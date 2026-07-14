#pragma once

// The GW2 API localizes most game-content text (item/map/skill names, etc.). The requested language is sent
// as `?lang=` (equivalently the `Accept-Language` header). Authenticated/account data ignores it. English is
// the default; the Connection carries one Locale for the whole client and the user could expose it later.
namespace Api
{
    enum class Locale { En, Es, De, Fr, Zh };

    inline const char* LocaleCode(Locale l)
    {
        switch (l)
        {
            case Locale::Es: return "es";
            case Locale::De: return "de";
            case Locale::Fr: return "fr";
            case Locale::Zh: return "zh";
            case Locale::En:
            default:         return "en";
        }
    }
}
