#pragma once
#include <cstdint>

// The MumbleLink framework - a typed wrapper over the GW2 shared-memory link that Nexus publishes at
// DL_MUMBLE_LINK (the binary LinkedMem) and DL_MUMBLE_LINK_IDENTITY (the parsed Identity). The raw byte
// layout matches the GW2 wiki spec (API:MumbleLink) and is the same proven layout the Pathing addon uses.
//
// This header owns the LAYOUT + the typed vocabulary (UIState flags, MapType / MountType / Profession / Race
// enums, the interface-size -> scale factor). It deliberately has NO dependency on the addon globals: it
// operates on raw field values, so it stays a pure, reusable description of the link. Shared.h holds the
// extern MumbleLink/MumbleIdent pointers and adds the thin "read the live link" convenience helpers on top.
namespace Mumble
{
    struct Vector3
    {
        float X, Y, Z;
    };

    // Context (game-supplied, in LinkedMem.Context). ServerAddress..MountIndex per the wiki layout.
    struct Context
    {
        uint8_t ServerAddress[28];
        uint32_t MapId;
        uint32_t MapType;
        uint32_t ShardId;
        uint32_t Instance;
        uint32_t BuildId;
        uint32_t UIState;      // bitfield - see UiState below
        uint16_t CompassWidth; // minimap size, LOGICAL (pre-interface-scale) pixels
        uint16_t CompassHeight;
        float CompassRotation;
        float PlayerX; // continent coords (map open)
        float PlayerY;
        float MapCenterX;
        float MapCenterY;
        float MapScale;
        uint32_t ProcessId;
        uint8_t MountIndex; // see MountType
    };

    struct LinkedMem
    {
        uint32_t UIVersion;
        uint32_t UITick;        // increments each game frame the link is written (freshness)
        Vector3 AvatarPosition; // world metres (Y up) - matches our trail/projection space
        Vector3 AvatarFront;
        Vector3 AvatarTop;
        wchar_t Name[256];
        Vector3 CameraPosition;
        Vector3 CameraFront;
        Vector3 CameraTop;
        wchar_t Identity[256]; // JSON; Nexus parses it for us into the Identity struct below
        uint32_t ContextLen;
        union
        {
            Context Context;
            uint8_t ContextRaw[256];
        };
        wchar_t Description[2048];
    };

    // The parsed Identity JSON (Nexus publishes this at DL_MUMBLE_LINK_IDENTITY).
    struct Identity
    {
        char Name[20];
        uint32_t Profession; // see Profession
        uint32_t Spec;       // elite-spec id (third specialization) - id-keyed, resolve via /v2/specializations
        uint32_t Race;       // see Race
        uint32_t MapID;
        uint32_t WorldID;
        uint32_t TeamColorID;
        bool IsCommander;
        float FOV;
        uint32_t UISize; // see UiSize
    };

    // --- UIState bitfield (Context.UIState), full wiki spec ---------------------------------------------
    namespace UiState
    {
        enum Bits : uint32_t
        {
            MapOpen = 0x01,         // the fullscreen map is open
            CompassTopRight = 0x02, // minimap is in the top-right corner (else bottom-right)
            CompassRotation = 0x04, // minimap rotation is enabled
            GameHasFocus = 0x08,    // the GW2 window has OS focus
            CompetitiveMode = 0x10, // in a competitive game mode (PvP / WvW)
            TextboxFocus = 0x20,    // a textbox (chat / search) has focus - swallow hotkeys
            InCombat = 0x40,        // the player is in combat
        };
        inline bool Has(uint32_t uiState, uint32_t bit) { return (uiState & bit) != 0; }
    }

    // --- Interface size -> scale factor (Identity.UISize). ----------------------------------------------
    enum class UiSize : uint32_t
    {
        Small = 0,
        Normal = 1,
        Large = 2,
        Larger = 3
    };
    inline float UiScale(uint32_t uiSize)
    {
        switch (uiSize)
        {
        case 0:
            return 0.900f; // Small
        case 2:
            return 1.111f; // Large
        case 3:
            return 1.222f; // Larger
        default:
            return 1.000f; // Normal (1) / unknown
        }
    }

    // --- Mount (Context.MountIndex) --------------------------------------------------------------------
    enum class MountType : uint8_t
    {
        None = 0,
        Jackal,
        Griffon,
        Springer,
        Skimmer,
        Raptor,
        RollerBeetle,
        Warclaw,
        Skyscale,
        Skiff,
        SiegeTurtle
    };
    inline MountType ToMount(uint8_t mountIndex)
    {
        return mountIndex <= (uint8_t)MountType::SiegeTurtle ? (MountType)mountIndex : MountType::None;
    }
    inline const char *MountName(uint8_t mountIndex)
    {
        switch (ToMount(mountIndex))
        {
        case MountType::Jackal:
            return "Jackal";
        case MountType::Griffon:
            return "Griffon";
        case MountType::Springer:
            return "Springer";
        case MountType::Skimmer:
            return "Skimmer";
        case MountType::Raptor:
            return "Raptor";
        case MountType::RollerBeetle:
            return "Roller Beetle";
        case MountType::Warclaw:
            return "Warclaw";
        case MountType::Skyscale:
            return "Skyscale";
        case MountType::Skiff:
            return "Skiff";
        case MountType::SiegeTurtle:
            return "Siege Turtle";
        default:
            return "None";
        }
    }

    // --- Profession (Identity.Profession; 0 = none) ----------------------------------------------------
    enum class Profession : uint32_t
    {
        None = 0,
        Guardian,
        Warrior,
        Engineer,
        Ranger,
        Thief,
        Elementalist,
        Mesmer,
        Necromancer,
        Revenant
    };
    inline const char *ProfessionName(uint32_t p)
    {
        switch ((Profession)p)
        {
        case Profession::Guardian:
            return "Guardian";
        case Profession::Warrior:
            return "Warrior";
        case Profession::Engineer:
            return "Engineer";
        case Profession::Ranger:
            return "Ranger";
        case Profession::Thief:
            return "Thief";
        case Profession::Elementalist:
            return "Elementalist";
        case Profession::Mesmer:
            return "Mesmer";
        case Profession::Necromancer:
            return "Necromancer";
        case Profession::Revenant:
            return "Revenant";
        default:
            return "Unknown";
        }
    }

    // --- Race (Identity.Race) --------------------------------------------------------------------------
    enum class Race : uint32_t
    {
        Asura = 0,
        Charr,
        Human,
        Norn,
        Sylvari
    };
    inline const char *RaceName(uint32_t r)
    {
        switch ((Race)r)
        {
        case Race::Asura:
            return "Asura";
        case Race::Charr:
            return "Charr";
        case Race::Human:
            return "Human";
        case Race::Norn:
            return "Norn";
        case Race::Sylvari:
            return "Sylvari";
        default:
            return "Unknown";
        }
    }

    // --- MapType (Context.MapType; mirrors Gw2Sharp Models.MapType) -------------------------------------
    enum class MapType : uint32_t
    {
        Redirect = 0,
        CharacterCreate,
        Pvp,
        Gvg,
        Instance,
        Public,
        Tournament,
        Tutorial,
        UserTournament,
        WvwEternalBattlegrounds,
        WvwBlueBorderlands,
        WvwGreenBorderlands,
        WvwRedBorderlands,
        WvwFortunesVale,
        WvwObsidianSanctum,
        WvwEdgeOfTheMists,
        PublicMini,
        BigInstance,
        WvwLounge
    };
    inline const char *MapTypeName(uint32_t t)
    {
        switch ((MapType)t)
        {
        case MapType::CharacterCreate:
            return "Character Create";
        case MapType::Pvp:
            return "PvP";
        case MapType::Gvg:
            return "GvG";
        case MapType::Instance:
            return "Instance";
        case MapType::Public:
            return "Public";
        case MapType::Tournament:
            return "Tournament";
        case MapType::Tutorial:
            return "Tutorial";
        case MapType::UserTournament:
            return "User Tournament";
        case MapType::WvwEternalBattlegrounds:
            return "WvW Eternal Battlegrounds";
        case MapType::WvwBlueBorderlands:
            return "WvW Blue Borderlands";
        case MapType::WvwGreenBorderlands:
            return "WvW Green Borderlands";
        case MapType::WvwRedBorderlands:
            return "WvW Red Borderlands";
        case MapType::WvwFortunesVale:
            return "WvW Fortune's Vale";
        case MapType::WvwObsidianSanctum:
            return "WvW Obsidian Sanctum";
        case MapType::WvwEdgeOfTheMists:
            return "WvW Edge of the Mists";
        case MapType::PublicMini:
            return "Public Mini";
        case MapType::BigInstance:
            return "Big Instance";
        case MapType::WvwLounge:
            return "WvW Lounge";
        case MapType::Redirect:
            return "Redirect";
        default:
            return "Unknown";
        }
    }

    // Is this map type an instanced (non-open-world) space? Open-world recommendation is useless inside one.
    inline bool IsInstancedMapType(uint32_t t)
    {
        return (MapType)t == MapType::Instance || (MapType)t == MapType::BigInstance;
    }
}
