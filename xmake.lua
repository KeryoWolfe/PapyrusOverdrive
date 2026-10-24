-- Papyrus Overdrive: an F4SE plugin for Fallout 4 AE (README.md).

-- CommonLibF4: this project's own copy (lib/commonlibf4, a git submodule:
-- `git submodule update --init --recursive`), or, where that isn't there,
-- the one in a 4Stim checkout next to this folder.
local commonlib = "lib/commonlibf4"
if not os.isfile(path.join(os.scriptdir(), commonlib, "xmake.lua")) then
    commonlib = "../4Stim/lib/commonlibf4"
end
includes(commonlib)

set_project("PapyrusOverdrive")
set_version("0.3.1")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
-- releasedbg unless `xmake f -m` says otherwise: optimized, with a .pdb
-- (plain "release" isn't one of the modes here and builds without one).
set_defaultmode("releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("PapyrusOverdrive")
    add_rules("commonlibf4.plugin", {
        name = "PapyrusOverdrive",
        author = "KeryoWolfe",
        description = "Lets read-only engine functions answer Papyrus scripts at once, and profiles native calls"
    })

    add_defines("PAPYRUSOVERDRIVE_VERSION_MAJOR=0", "PAPYRUSOVERDRIVE_VERSION_MINOR=3", "PAPYRUSOVERDRIVE_VERSION_PATCH=1")  -- keep in step with set_version

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
    add_installfiles("Data/(F4SE/Plugins/*.ini)")
