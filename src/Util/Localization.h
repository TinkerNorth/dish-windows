#pragma once

#include <QString>
#include <QStringLiteral>

class QLocale;
class QTranslator;

namespace dish::i18n {

// Where qt_add_translations() drops the compiled catalogues inside the binary.
// The .qm family is `dish_<locale>.qm` — see the i18n block in CMakeLists.txt.
inline QString catalogDirectory()
{
    return QStringLiteral(":/i18n");
}

inline QString catalogPrefix()
{
    return QStringLiteral("dish");
}

// Loads the catalogue that best matches `locale` into `translator`, returning
// false when the locale has no catalogue and the caller should stay on the
// source strings.
//
// This routes through QTranslator's locale-aware overload rather than a
// filename guess, which buys two things a hand-rolled `load("dish_de_DE")`
// does not. It walks QLocale::uiLanguages(), so a machine whose system locale
// is en_US but whose preferred UI language is German still gets German — that
// list is exactly what Windows' language settings populate. And it does the
// region fallback itself, so de_DE finds dish_de.qm while pt_BR still prefers
// dish_pt_BR.qm over dish_pt.qm.
//
// `directory` is a seam for the tests, which load the .qm straight off the
// build tree because the QRC that holds them belongs to the Dish executable
// and not to dish_core.
bool loadCatalog(QTranslator& translator, const QLocale& locale,
                 const QString& directory = catalogDirectory());

}  // namespace dish::i18n
