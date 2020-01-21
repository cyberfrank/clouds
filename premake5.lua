-- version: premake-5.0.0-alpha13

-- %TM_PRE_ALPHA_DIR% should be set to the directory of the pre-alpha package

workspace "clouds"
    configurations {"Debug", "Release"}
    language "C++"
    cppdialect "C++11"
    flags { "FatalWarnings", "MultiProcessorCompile" }
    warnings "Extra"
    inlining "Auto"
    sysincludedirs { "" }
    targetdir "bin/%{cfg.buildcfg}"

filter "system:windows"
    platforms { "Win64" }
    systemversion("10.0")

filter { "system:macosx" }
    platforms { "MacOSX" }

filter { "platforms:MacOSX" }
    defines { "TM_OS_POSIX", "TM_OS_MACOSX", "TM_NO_MAIN_FIBER" }
    includedirs { "$(TM_PRE_ALPHA_DIR)/headers" }
    architecture "x64"
    libdirs { "$(TM_PRE_ALPHA_DIR)/lib/%{cfg.buildcfg}"}
    buildoptions {
        "-fms-extensions",              -- Allow anonymous struct as C inheritance.
    }
    disablewarnings {
        "missing-field-initializers",   -- = {0} is OK.
        "unknown-pragmas",              -- We use this for docgen.
        "unused-parameter",             -- Useful for documentation purposes.
        "unused-local-typedef",         -- We don't always use all typedefs.
        "missing-braces",               -- = {0} is OK.
        "microsoft-anon-tag",           -- Allow anonymous structs.
    }

filter "platforms:Win64"
    defines { "TM_OS_WINDOWS", "_CRT_SECURE_NO_WARNINGS" }
    includedirs { "%TM_PRE_ALPHA_DIR%/headers" }
    staticruntime "On"
    architecture "x64"
    libdirs { "%TM_PRE_ALPHA_DIR%/lib/%{cfg.buildcfg}"}
    disablewarnings {
        "4057", -- Slightly different base types. Converting from type with volatile to without.
        "4068", -- Unknown pragma. We use these for docgen.
        "4100", -- Unused formal parameter. I think unusued parameters are good for documentation.
        "4152", -- Conversion from function pointer to void *. Should be ok.
        "4200", -- Zero-sized array. Valid C99.
        "4201", -- Nameless struct/union. Valid C11.
        "4204", -- Non-constant aggregate initializer. Valid C99.
        "4206", -- Translation unit is empty. Might be #ifdefed out.
        "4214", -- Bool bit-fields. Valid C99.
        "4221", -- Pointers to locals in initializers. Valid C99.
        "4456", -- Declaration hides previous local declaration. TODO: Fix this!
        "4457", -- Declaration hides function parameter. TODO: Fix this!
        "4459", -- Declaration hides global declaration. TODO: Fix this!
        "4702", -- Unreachable code. We sometimes want return after exit() because otherwise we get an error about no return value.
    }
    linkoptions {"/ignore:4099"}

filter "configurations:Debug"
    defines { "TM_CONFIGURATION_DEBUG", "DEBUG" }
    symbols "On"

filter "configurations:Release"
    defines { "TM_CONFIGURATION_RELEASE" }
    optimize "On"

project "cloud-component"
    location "build/cloud_component"
    kind "SharedLib"
    language "C++"
    targetdir "bin/%{cfg.buildcfg}/plugins"
    dependson {"foundation", "entity"}
    files { "plugins/cloud_component/**.inl", "plugins/cloud_component/**.h", "plugins/cloud_component/**.c", "plugins/cloud_component/**.shader" }
    filter "platforms:Win64"
        targetdir "$(TM_PRE_ALPHA_DIR)/bin/plugins"
    filter { "platforms:MacOSX" }
        targetdir "%TM_PRE_ALPHA_DIR%/bin/plugins"