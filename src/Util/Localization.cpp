#include "Util/Localization.h"

#include <QLocale>
#include <QTranslator>

namespace dish::i18n {

bool loadCatalog(QTranslator& translator, const QLocale& locale, const QString& directory)
{
    return translator.load(locale, catalogPrefix(), QStringLiteral("_"), directory,
                           QStringLiteral(".qm"));
}

}  // namespace dish::i18n
