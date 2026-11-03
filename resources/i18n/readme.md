# Application translations

English is the source language. Wrap product text in QML qsTr(), C++ tr(), or
QT_TRANSLATE_NOOP with a stable context. Keep protocol values, error codes and
user supplied names unchanged. Maintain Chinese translations in quickclash_zh_cn.ts;
do not put translated text back into product source files.

Update the catalog after adding text:

    C:/Qt/6.9.0/msvc2022_64/bin/lupdate.exe app src qml -no-obsolete -locations none -ts resources/i18n/quickclash_zh_cn.ts

Complete every translation in Qt Linguist, then build normally. CMake runs lrelease
and embeds the application catalog and Qt base/declarative Chinese catalogs.
Qt LinguistTools and the Qt translation data must be installed on the build host;
the deployed app needs no SDK or network language download.

At startup Localization reads QLocale::system().uiLanguages() in preference order.
English variants use source text; all Chinese variants currently share Simplified
Chinese. If no supported language matches, English is used. The Appearance section
also offers Follow system, English and Simplified Chinese as a saved preference.
After the preference is saved, the running UI, navigation, derived status text and
tray menu are retranslated in place. Restart after changing the system language in
Follow system mode. Unit tests cover preference order, explicit selection and fallback.
The development-only --probe-language option requires isolated mode and is used
for repeatable UI evidence; it is not a saved user preference.

The committed catalog is curated and may not match a wholesale lupdate rewrite.
Review the diff and merge new or changed messages instead of replacing the file.
