#include "infra/config/config_metadata.h"
#include "infra/io/device_stream.h"
#include <QStringList>
#include <set>
#include <sstream>
#include <vector>
#include <yaml-cpp/emitfromevents.h>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
struct InvalidMetadata {
    const char* code;
};
void require(bool condition, const char* code = "InvalidYaml") {
    if (!condition)
        throw InvalidMetadata{code};
}
class MetadataReader final : public YAML::EmitFromEvents {
  public:
    MetadataReader(YAML::Emitter& output, bool separateRules)
        : YAML::EmitFromEvents(output), output_(output), separateRules_(separateRules) {}
    MetadataReader(YAML::Emitter& output, std::string field,
                   std::function<bool(const std::string&)> sink)
        : YAML::EmitFromEvents(output), output_(output), separateRules_(true),
          scalarField_(std::move(field)), sink_(std::move(sink)), streaming_(true) {}
    ConfigMetadata result;
    void OnDocumentStart(const YAML::Mark&) override {
        require(++documents_ == 1);
    }
    void OnDocumentEnd() override {}
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override {
        throw InvalidMetadata{"UnsupportedConfigFeature"};
    }
    void OnNull(const YAML::Mark& mark, YAML::anchor_t) override {
        count();
        require(!frames_.empty() && (!frames_.back().map || !frames_.back().key));
        require(!collectingRules_, "InvalidConfigField");
        if (selected_)
            YAML::EmitFromEvents::OnNull(mark, 0);
        complete();
    }
    void OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t,
                  const std::string& value) override {
        count();
        if (streaming_) {
            require(value.size() <= 8192, "ConfigLimitExceeded");
            require(tag.empty() || tag == "?" || tag == "!" || tag == "tag:yaml.org,2002:str" ||
                        tag == "tag:yaml.org,2002:bool" || tag == "tag:yaml.org,2002:int" ||
                        tag == "tag:yaml.org,2002:float",
                    "UnsupportedConfigFeature");
            require(output_.size() + value.size() * 6 + 64 <= 4 * 1024 * 1024,
                    "ConfigLimitExceeded");
        }
        require(!frames_.empty());
        auto& parent = frames_.back();
        if (parent.map && parent.key) {
            require(parent.keys.insert(value).second, "DuplicateConfigKey");
            if (frames_.size() == 1) {
                field_ = value;
                result.keys.insert(QString::fromStdString(value));
                selected_ = separateRules_ || value == "proxies" || value == "proxy-groups" ||
                            value == "dns" || value == "proxy-providers" ||
                            value == "rule-providers";
            }
        } else if (field_ == scalarField_ && frames_.size() == 2 && !parent.map) {
            if (collectingRules_) {
                require(++scalarCount_ <= 20000, "ConfigLimitExceeded");
                if (sink_)
                    require(sink_(value), "DataWriteFailed");
                else
                    result.rules.push_back(value);
            }
            result.geoRule =
                result.geoRule || value.starts_with("GEOIP,") || value.starts_with("GEOSITE,");
            if (result.defaultPolicy.isEmpty()) {
                const auto parts = QString::fromStdString(value).split(',');
                if (parts.size() >= 2 &&
                    (parts[0].trimmed() == "MATCH" || parts[0].trimmed() == "FINAL"))
                    result.defaultPolicy = parts[1].trimmed();
            }
        }
        if (selected_ && !collectingRules_)
            YAML::EmitFromEvents::OnScalar(mark, tag, 0, value);
        complete();
    }
    void OnSequenceStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t,
                         YAML::EmitterStyle::value style) override {
        checkTag(tag, "tag:yaml.org,2002:seq");
        require(!collectingRules_, "InvalidConfigField");
        enter(false);
        if (selected_)
            YAML::EmitFromEvents::OnSequenceStart(mark, tag, 0, style);
        collectingRules_ = separateRules_ && field_ == scalarField_ && frames_.size() == 2;
    }
    void OnSequenceEnd() override {
        if (selected_)
            YAML::EmitFromEvents::OnSequenceEnd();
        frames_.pop_back();
        collectingRules_ = false;
        complete();
    }
    void OnMapStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t,
                    YAML::EmitterStyle::value style) override {
        checkTag(tag, "tag:yaml.org,2002:map");
        require(!collectingRules_, "InvalidConfigField");
        enter(true);
        if (frames_.size() == 1 || selected_)
            YAML::EmitFromEvents::OnMapStart(mark, tag, 0, style);
    }
    void OnMapEnd() override {
        if (frames_.size() == 1 || selected_)
            YAML::EmitFromEvents::OnMapEnd();
        frames_.pop_back();
        complete();
    }

  private:
    struct Frame {
        bool map;
        bool key = true;
        std::set<std::string> keys;
    };
    void checkTag(const std::string& tag, const std::string& expected) const {
        if (streaming_)
            require(tag.empty() || tag == "?" || tag == "!" || tag == expected,
                    "UnsupportedConfigFeature");
    }
    void count() {
        require(++events_ <= 100000, "ConfigLimitExceeded");
        result.events = events_;
        if (streaming_)
            require(output_.size() <= 4 * 1024 * 1024, "ConfigLimitExceeded");
    }
    void enter(bool map) {
        count();
        require(frames_.size() < 32, "ConfigLimitExceeded");
        require(frames_.empty() ? map : (!frames_.back().map || !frames_.back().key));
        frames_.push_back({map, true, {}});
    }
    void complete() {
        if (!frames_.empty() && frames_.back().map)
            frames_.back().key = !frames_.back().key;
        if (frames_.size() == 1 && frames_.back().key)
            selected_ = false;
    }
    std::vector<Frame> frames_;
    YAML::Emitter& output_;
    std::string field_;
    bool selected_ = false;
    bool separateRules_ = false;
    bool collectingRules_ = false;
    std::string scalarField_ = "rules";
    std::function<bool(const std::string&)> sink_;
    bool streaming_ = false;
    std::size_t scalarCount_ = 0;
    int documents_ = 0;
    std::size_t events_ = 0;
};
} // namespace
domain::Result<ConfigMetadata> configMetadata(const QByteArray& input, bool separateRules) {
    try {
        require(input.size() <= 16 * 1024 * 1024, "ConfigLimitExceeded");
        std::istringstream stream(input.toStdString());
        YAML::Parser parser(stream);
        YAML::Emitter output;
        MetadataReader reader(output, separateRules);
        require(parser.HandleNextDocument(reader));
        while (parser.HandleNextDocument(reader)) {
        }
        require(output.good());
        reader.result.fieldsYaml =
            QByteArray(output.c_str(), static_cast<qsizetype>(output.size()));
        return reader.result;
    } catch (const InvalidMetadata& error) {
        domain::Error failure;
        failure.code = QString::fromLatin1(error.code);
        failure.userMessageKey = failure.code;
        failure.stage = QStringLiteral("preflight");
        return failure;
    } catch (const YAML::Exception&) {
        domain::Error failure;
        failure.code = QStringLiteral("InvalidYaml");
        failure.userMessageKey = failure.code;
        failure.stage = QStringLiteral("preflight");
        return failure;
    }
}
domain::Result<ConfigMetadata>
scanConfigScalars(QIODevice& input, const std::string& field,
                  const std::function<bool(const std::string&)>& sink, qint64 limit) {
    try {
        DeviceStreamBuffer buffer(input, limit);
        std::istream stream(&buffer);
        YAML::Parser parser(stream);
        YAML::Emitter output;
        MetadataReader reader(output, field, sink);
        require(parser.HandleNextDocument(reader));
        while (parser.HandleNextDocument(reader)) {
        }
        require(buffer.complete(), "DataReadFailed");
        require(output.good() && output.size() <= 4 * 1024 * 1024, "ConfigLimitExceeded");
        reader.result.fieldsYaml =
            QByteArray(output.c_str(), static_cast<qsizetype>(output.size()));
        return std::move(reader.result);
    } catch (const InvalidMetadata& error) {
        domain::Error failure;
        failure.code = QString::fromLatin1(error.code);
        failure.userMessageKey = failure.code;
        failure.stage = QStringLiteral("preflight");
        return failure;
    } catch (const YAML::Exception&) {
        domain::Error failure;
        failure.code = QStringLiteral("InvalidYaml");
        failure.userMessageKey = failure.code;
        failure.stage = QStringLiteral("preflight");
        return failure;
    }
}
} // namespace quickclash::infra
