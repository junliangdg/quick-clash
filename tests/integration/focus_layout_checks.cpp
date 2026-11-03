#include "focus_layout_checks.h"
#include <QCoreApplication>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <cmath>
#include <cstdio>

namespace quickclash::test {
namespace {
QList<QQuickItem*> visualItems(QQuickItem* root) {
    QList<QQuickItem*> result{root};
    for (auto* child : root->childItems())
        result.append(visualItems(child));
    return result;
}
bool frameFits(QQuickItem* control, bool vertical) {
    bool found = false;
    for (auto* ring : visualItems(control)) {
        if (ring->objectName() != "focus_ring" || !ring->isVisible())
            continue;
        found = true;
        for (auto* ancestor = ring->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
            if (!ancestor->clip())
                continue;
            const auto frame = ring->mapRectToItem(ancestor, ring->boundingRect());
            const auto bounds = ancestor->boundingRect().adjusted(-0.75, -0.75, 0.75, 0.75);
            if (frame.left() < bounds.left() || frame.right() > bounds.right() ||
                (vertical && (frame.top() < bounds.top() || frame.bottom() > bounds.bottom()))) {
                std::fprintf(
                    stderr,
                    "Focus clipped: %s (%s), text=%s ancestor=%s frame=%.1f,%.1f %.1fx%.1f "
                    "bounds=%.1fx%.1f\n",
                    control->objectName().toUtf8().constData(), control->metaObject()->className(),
                    control->property("text").toString().toUtf8().constData(),
                    ancestor->metaObject()->className(), frame.x(), frame.y(), frame.width(),
                    frame.height(), bounds.width(), bounds.height());
                for (auto* item = control; item; item = item->parentItem())
                    std::fprintf(stderr, "  %s %s: %.1f,%.1f %.1fx%.1f implicit %.1fx%.1f\n",
                                 item->metaObject()->className(),
                                 item->objectName().toUtf8().constData(), item->x(), item->y(),
                                 item->width(), item->height(), item->implicitWidth(),
                                 item->implicitHeight());
                return false;
            }
        }
        const auto frame = ring->mapRectToScene(ring->boundingRect());
        const auto bounds =
            control->window()->contentItem()->boundingRect().adjusted(-0.75, -0.75, 0.75, 0.75);
        if (frame.left() < bounds.left() || frame.right() > bounds.right() ||
            (vertical && (frame.top() < bounds.top() || frame.bottom() > bounds.bottom()))) {
            std::fprintf(stderr,
                         "Focus outside window: %s (%s), text=%s frame=%.1f,%.1f %.1fx%.1f "
                         "bounds=%.1fx%.1f\n",
                         control->objectName().toUtf8().constData(),
                         control->metaObject()->className(),
                         control->property("text").toString().toUtf8().constData(), frame.x(),
                         frame.y(), frame.width(), frame.height(), bounds.width(), bounds.height());
            for (auto* item = control; item; item = item->parentItem())
                std::fprintf(stderr, "  %s %s: %.1f,%.1f %.1fx%.1f implicit %.1fx%.1f\n",
                             item->metaObject()->className(),
                             item->objectName().toUtf8().constData(), item->x(), item->y(),
                             item->width(), item->height(), item->implicitWidth(),
                             item->implicitHeight());
            return false;
        }
    }
    // Some Qt controls render their focus/selection internally, not via StateSurface.
    if (!found && control->property("theme").isValid()) {
        std::fprintf(stderr, "Missing visible focus ring: %s activeFocus=%d windowActive=%d\n",
                     control->objectName().toUtf8().constData(), control->hasActiveFocus(),
                     control->window()->isActive());
        return false;
    }
    return true;
}
} // namespace
bool checkScrollLayouts(QQuickWindow* window) {
    int checked = 0;
    for (auto* view : visualItems(window->contentItem())) {
        auto* form = view->property("form").value<QQuickItem*>();
        if (!form || !view->property("focusMargin").isValid() || !view->isVisible() ||
            view->width() < 1 || view->height() < 1)
            continue;
        const auto margin = view->property("focusMargin").toReal();
        const auto width = view->property("formWidth").toReal();
        const auto bar = view->property("effectiveScrollBarWidth").toReal();
        const auto barPadding =
            view->property(view->property("mirrored").toBool() ? "leftPadding" : "rightPadding")
                .toReal();
        if (!view->clip() || margin < 8 || std::abs(form->x() - margin) > 0.5 ||
            std::abs(form->y() - margin) > 0.5 || std::abs(form->width() - width) > 0.5 ||
            barPadding + 0.5 < bar ||
            view->property("contentHeight").toReal() + 0.5 < form->implicitHeight() + 2 * margin) {
            std::fprintf(stderr, "Unsafe scroll layout: %s (%s), body %.1f,%.1f %.1f margin=%.1f\n",
                         view->objectName().toUtf8().constData(), view->metaObject()->className(),
                         form->x(), form->y(), form->width(), margin);
            return false;
        }
        auto* verticalBar = view->findChild<QQuickItem*>("themed_vertical_scroll_bar");
        const auto expectedX = view->property("mirrored").toBool()
                                   ? 0.0
                                   : view->width() - (verticalBar ? verticalBar->width() : 0.0);
        if (!verticalBar || verticalBar->parentItem() != view || verticalBar->width() <= 0 ||
            verticalBar->property("orientation").toInt() != Qt::Vertical ||
            std::abs(verticalBar->x() - expectedX) > 0.5 ||
            std::abs(verticalBar->y() - view->property("topPadding").toReal()) > 0.5 ||
            std::abs(verticalBar->height() - view->property("availableHeight").toReal()) > 0.5 ||
            barPadding + 0.5 < verticalBar->width()) {
            std::fprintf(stderr,
                         "Misplaced vertical scrollbar: %s (%s), bar %.1f,%.1f %.1fx%.1f "
                         "expected %.1f,%.1f height=%.1f parentMatches=%d\n",
                         view->objectName().toUtf8().constData(), view->metaObject()->className(),
                         verticalBar ? verticalBar->x() : -1, verticalBar ? verticalBar->y() : -1,
                         verticalBar ? verticalBar->width() : -1,
                         verticalBar ? verticalBar->height() : -1, expectedX,
                         view->property("topPadding").toReal(),
                         view->property("availableHeight").toReal(),
                         verticalBar && verticalBar->parentItem() == view);
            return false;
        }
        ++checked;
    }
    (void)checked;
    return true;
}
bool checkPageAlignment(QQuickWindow* window) {
    auto* host = window->findChild<QQuickItem*>("active_page_host");
    auto* title = window->findChild<QQuickItem*>("page_title_row");
    auto* page = host ? host->property("item").value<QQuickItem*>() : nullptr;
    if (!page || !title || !page->isVisible()) {
        std::fprintf(stderr, "Page alignment fixture unavailable\n");
        return false;
    }
    auto* content = page->property("form").value<QQuickItem*>();
    if (!content && page->objectName() == "subscriptions_page")
        content = page->findChild<QQuickItem*>("subscription_content_header");
    if (!content && !page->findChildren<QQuickItem*>("proxy_content_column").isEmpty())
        content = page->findChild<QQuickItem*>("proxy_content_column");
    if (!content)
        content = page;
    const auto heading = title->mapRectToScene(title->boundingRect());
    const auto body = content->mapRectToScene(content->boundingRect());
    if (std::abs(heading.left() - body.left()) > 0.75 ||
        std::abs(heading.right() - body.right()) > 0.75) {
        std::fprintf(stderr,
                     "Page alignment mismatch: %s (%s), content=%s (%s), "
                     "body=%.1f..%.1f heading=%.1f..%.1f hostWidth=%.1f outsets=%.1f,%.1f\n",
                     page->objectName().toUtf8().constData(), page->metaObject()->className(),
                     content->objectName().toUtf8().constData(), content->metaObject()->className(),
                     body.left(), body.right(), heading.left(), heading.right(), host->width(),
                     host->property("pageLeftOutset").toReal(),
                     host->property("pageRightOutset").toReal());
        return false;
    }
    const auto margin = window->property("pageContentMargin").toReal();
    const auto navigation = window->property("navigationWidth").toReal();
    const auto left = heading.left() - navigation;
    const auto right = window->width() - heading.right();
    if (std::abs(left - right) > 0.75 || left + 0.75 < margin) {
        std::fprintf(stderr, "Page margins asymmetric: left=%.1f right=%.1f minimum=%.1f\n", left,
                     right, margin);
        return false;
    }
    QQuickItem* bar = nullptr;
    if (page->property("form").value<QQuickItem*>()) {
        for (auto* candidate : page->findChildren<QQuickItem*>("themed_vertical_scroll_bar"))
            if (candidate->parentItem() == page)
                bar = candidate;
        if (!bar) {
            std::fprintf(stderr, "Page scrollbar unavailable\n");
            return false;
        }
    }
    if (!bar && page->objectName() == "subscriptions_page")
        bar = page->findChild<QQuickItem*>("subscription_page_bar");
    if (!bar && page->findChild<QQuickItem*>("proxy_page_bar"))
        bar = page->findChild<QQuickItem*>("proxy_page_bar");
    if (bar && bar->isVisible()) {
        const auto track = bar->mapRectToScene(bar->boundingRect());
        const auto mirrored = page->property("mirrored").toBool();
        if ((mirrored && track.right() > body.left() + 0.75) ||
            (!mirrored && track.left() < body.right() - 0.75) || track.left() < navigation - 0.75 ||
            track.right() > window->width() + 0.75) {
            std::fprintf(stderr, "Scrollbar not in page margin: bar=%.1f..%.1f body=%.1f..%.1f\n",
                         track.left(), track.right(), body.left(), body.right());
            return false;
        }
    }
    return true;
}
bool checkVisibleFocusFrames(QQuickWindow* window) {
    // Snapshotting forces Qt Quick to polish and render pending native layout changes.
    // Inspect the geometry of the frame the user can actually see, not a stale layout.
    if (window->grabWindow().isNull()) {
        std::fprintf(stderr, "Unable to render focus layout frame\n");
        return false;
    }
    const QPointer<QQuickItem> previous = window->activeFocusItem();
    bool safe = true;
    for (auto* control : visualItems(window->contentItem())) {
        if (!control->isVisible() || !control->isEnabled() || !control->activeFocusOnTab() ||
            control->width() < 1 || control->height() < 1 ||
            !window->contentItem()->boundingRect().intersects(
                control->mapRectToScene(control->boundingRect())))
            continue;
        // Ignore controls outside a scrolled pane. Wide virtual rows still intersect and are
        // checked.
        bool visible = true;
        for (auto* ancestor = control->parentItem(); ancestor; ancestor = ancestor->parentItem())
            if (ancestor->clip() && !ancestor->boundingRect().intersects(
                                        control->mapRectToItem(ancestor, control->boundingRect())))
                visible = false;
        if (!visible)
            continue;
        control->forceActiveFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        if (!frameFits(control, false))
            safe = false;
    }
    if (previous && previous->isVisible() && previous->isEnabled())
        previous->forceActiveFocus(Qt::OtherFocusReason);
    return checkScrollLayouts(window) && checkPageAlignment(window) && safe;
}
bool focusInScroll(QQuickItem* control) {
    if (!control || !control->isVisible() || !control->isEnabled()) {
        std::fprintf(stderr, "Focus target unavailable: %s visible=%d enabled=%d\n",
                     control ? control->objectName().toUtf8().constData() : "null",
                     control && control->isVisible(), control && control->isEnabled());
        return false;
    }
    if (!control->window()->isActive()) {
        control->window()->requestActivate();
        if (!QTest::qWaitForWindowActive(control->window(), 1000))
            return false;
    }
    // Reveal from the inner scroll outward, leaving the shared safety margin visible.
    for (auto* ancestor = control->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->property("contentY").isValid() ||
            !ancestor->property("contentHeight").isValid())
            continue;
        const auto y = control->mapToItem(ancestor, QPointF{}).y();
        const auto bottom =
            qMax(0.0, ancestor->property("contentHeight").toReal() - ancestor->height());
        ancestor->setProperty(
            "contentY", qBound(0.0, ancestor->property("contentY").toReal() + y - 12, bottom));
    }
    control->forceActiveFocus(Qt::TabFocusReason);
    QTest::qWait(20);
    return frameFits(control, true);
}
} // namespace quickclash::test
