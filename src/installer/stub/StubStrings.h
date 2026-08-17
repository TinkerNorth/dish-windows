// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The stub's entire user-facing vocabulary: three strings in the six app
// locales, as compile-time wide literals. The stub is deliberately outside the
// Qt translation pipeline (it must run with nothing installed), so this table
// is the documented i18n boundary — every other installer string is qsTr() in
// QML. Keep the locale set in step with translations/dish_*.ts and the
// register in step with each catalogue (de: du-form, bs: vi-form, es: tu-form,
// fr: vous-form, pt_BR: voce-form).

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dish::installer::stub {

// Window/dialog caption everywhere; the product name is never localized.
inline constexpr wchar_t kDialogTitle[] = L"Dish Setup";

struct StubStrings {
    const wchar_t* localePrefix; // BCP-47 primary subtag this row serves
    const wchar_t* preparing;    // marquee status line while extracting
    const wchar_t* damaged;      // payload integrity failure
    const wchar_t* osTooOld;     // OS gate failure (needs Win10 1809+ x64)
};

inline constexpr StubStrings kStubStrings[] = {
    {L"en", L"Preparing Dish Setup…", L"This download is damaged. Get a fresh copy and try again.",
     L"Dish needs Windows 10 version 1809 (64-bit) or newer."},
    {L"bs", L"Priprema Dish instalacije…",
     L"Ovo preuzimanje je oštećeno. Preuzmite novu kopiju i pokušajte ponovo.",
     L"Dish zahtijeva 64-bitni Windows 10, verziju 1809 ili noviju."},
    {L"de", L"Dish Setup wird vorbereitet…",
     L"Dieser Download ist beschädigt. Lade eine neue Kopie herunter und versuche es erneut.",
     L"Dish benötigt Windows 10 Version 1809 (64 Bit) oder neuer."},
    {L"es", L"Preparando la instalación de Dish…",
     L"Esta descarga está dañada. Descarga una copia nueva e inténtalo de nuevo.",
     L"Dish necesita Windows 10 versión 1809 (64 bits) o posterior."},
    {L"fr", L"Préparation de l’installation de Dish…",
     L"Ce téléchargement est endommagé. Téléchargez une nouvelle copie et réessayez.",
     L"Dish nécessite Windows 10 version 1809 (64 bits) ou ultérieure."},
    {L"pt", L"Preparando a instalação do Dish…",
     L"Este download está corrompido. Baixe uma nova cópia e tente novamente.",
     L"O Dish precisa do Windows 10 versão 1809 (64 bits) ou mais recente."},
};

// Walks the user's preferred UI languages in order and returns the first row
// whose primary subtag matches ("pt" serves pt_BR — Brazilian is the only
// Portuguese catalogue, matching the app's fallback). English otherwise.
inline const StubStrings& stringsForUserLocale() {
    ULONG numLanguages = 0;
    ULONG bufferChars = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLanguages, nullptr, &bufferChars) &&
        bufferChars > 0 && bufferChars <= 1024) {
        wchar_t buffer[1024];
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLanguages, buffer, &bufferChars)) {
            // buffer is a double-NUL-terminated list of names like "de-DE".
            for (const wchar_t* lang = buffer; *lang != L'\0';) {
                for (const StubStrings& row : kStubStrings) {
                    size_t n = 0;
                    while (row.localePrefix[n] != L'\0') ++n;
                    bool match = true;
                    for (size_t i = 0; i < n; ++i) {
                        const wchar_t c = lang[i];
                        const wchar_t want = row.localePrefix[i];
                        const wchar_t folded =
                            (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
                        if (folded != want) {
                            match = false;
                            break;
                        }
                    }
                    // Primary-subtag match only: "de" takes "de" or "de-AT",
                    // never "den...".
                    if (match && (lang[n] == L'\0' || lang[n] == L'-')) return row;
                }
                while (*lang != L'\0') ++lang;
                ++lang;
            }
        }
    }
    return kStubStrings[0]; // en
}

} // namespace dish::installer::stub
