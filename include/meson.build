project(
    'Apotris',
    'c',
    'cpp',
    version: '4.2.0a',
    license: 'AGPL3',
    meson_version: '>=1.1.0',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++17',
        'default_library=static',
        'warning_level=0',
        'b_lto=false',
    ],
)

# The name of your project.
name = 'Apotris'

# Define the platform-specific directories
src_dir = 'source'
inc_dir = 'include'
data_dir = 'data'
audio_dir = 'assets'
shaders_dir = 'assets/shaders'

# Include directories
includes = [
    'include',
    data_dir,
]

# Bring in all source files
sources = files(
    src_dir / 'audio_files.cpp',
    src_dir / 'blockEngine.cpp',
    src_dir / 'bot.cpp',
    src_dir / 'descriptions.cpp',
    src_dir / 'detectEmulators.c',
    src_dir / 'flashSaves.cpp',
    src_dir / 'game.cpp',
    src_dir / 'gbp_logo.cpp',
    src_dir / 'interrupt.cpp',
    src_dir / 'liba_gba.cpp',
    src_dir / 'liba_mm.cpp',
    src_dir / 'liba_pc.cpp',
    src_dir / 'liba_switch.cpp',
    src_dir / 'liba_vita.cpp',
    src_dir / 'liba_portmaster.cpp',
    src_dir / 'liba_web.cpp',
    src_dir / 'liba_window.cpp',
    src_dir / 'liba_android.cpp',
    src_dir / 'liba_sdl_audio.cpp',
    src_dir / 'LinkWireless.cpp',
    src_dir / 'main.cpp',
    src_dir / 'menuSystem.cpp',
    src_dir / 'menus.cpp',
    src_dir / 'multiplayer.cpp',
    src_dir / 'multiplayerClasses.cpp',
    src_dir / 'music.cpp',
    src_dir / 'prng.cpp',
    src_dir / 'qrcodegen.c',
    src_dir / 'rumblePatterns.cpp',
    src_dir / 'saving.cpp',
    src_dir / 'scenario.cpp',
    src_dir / 'scene.cpp',
    src_dir / 'screenAudio.cpp',
    src_dir / 'screenControls.cpp',
    src_dir / 'screenCredits.cpp',
    src_dir / 'screenException.cpp',
    src_dir / 'screenMultiplayerResults.cpp',
    src_dir / 'screenGraphics.cpp',
    src_dir / 'screenHandling.cpp',
    src_dir / 'screenLinks.cpp',
    src_dir / 'screenPlayOptions.cpp',
    src_dir / 'screenSkinEditor.cpp',
    src_dir / 'screenColorEditor.cpp',
    src_dir / 'skinFile.cpp',
    src_dir / 'stb_impl.cpp',
    src_dir / 'screenTitle.cpp',
    src_dir / 'sprites.cpp',
    src_dir / 'tetromino.cpp',
    src_dir / 'text.cpp',
    src_dir / 'nanotime.c',
    src_dir / 'shader.cpp',
    src_dir / 'gl.c',
    src_dir / 'gles2.c',
    src_dir / 'xoshiro128plusplus.c',
)

# Generate version header
configure_file(
    input: 'include/version.h.in',
    output: 'version.h',
    configuration: {
        'version': meson.project_version(),
    },
)

# Bring in binary/data files
subdir(data_dir)

# Common build file references
icon_file = files('sprites/favicon32.bmp')
effect_locations_file = 'effect_locations.bin'

build_type = get_option('buildtype')

# Set up recommended compiler and linker flags for GBA before loading any other projects
if host_machine.system() == 'gba'
    subdir('meson/toolchain')
    add_project_arguments('-Wno-incompatible-pointer-types', language: 'c')
    add_global_arguments('-Wno-implicit-function-declaration', language: 'c')
    #add_project_arguments('-DLINK_WIRELESS_PUT_ISR_IN_IWRAM=1', language: 'cpp')
    if build_type == 'release'
        add_global_link_arguments('-s', language: ['c', 'cpp'])
    endif
endif

if host_machine.system() == 'windows'
    add_global_arguments('-DWIN32', language: ['c', 'cpp'])
    add_global_arguments('-mwin32', language: ['c', 'cpp'])
elif host_machine.system() == 'emscripten'
    add_global_arguments('-pthread', language: ['cpp', 'c'])
    add_global_arguments('-fno-stack-protector', language: ['cpp', 'c'])
    add_global_arguments('-U_FORTIFY_SOURCE', language: ['cpp', 'c'])
elif host_machine.system() == 'darwin'
    add_global_arguments('-mmacosx-version-min=10.15', language : ['cpp', 'c'])
elif host_machine.system() == 'android'
    add_global_arguments('-DIMGUI_IMPL_OPENGL_ES3', language : ['c', 'cpp'])
endif

if host_machine.system() == 'n3ds'

    # Avoid issue with breaking stdlib synchronization
    # (https://github.com/devkitPro/gcc/issues/5)
    add_global_arguments(
        [
            '-D_GLIBCXX_GTHREAD_USE_WEAK=0',
        ],
        language: ['cpp'],
    )

    add_global_arguments(
        [
            '-I' + meson.current_source_dir() / 'subprojects/libctru/libctru/include',
        ],
        language: ['c', 'cpp'],
    )

    if build_type == 'release' or build_type == 'minsize'
        add_global_arguments(['-ffunction-sections', '-fdata-sections'], language: ['c', 'cpp'])
        add_global_link_arguments(['-Wl,--gc-sections'], language: ['c', 'cpp'])
    endif

    enable_debug_ui = get_option('debug_ui')
    if enable_debug_ui
        add_global_arguments(
            [
                '-DImTextureID=void*',
                '-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1',
                '-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS=1',
            ],
            language: ['cpp'],
        )
        add_project_arguments('-DDEBUG_UI', language: ['c', 'cpp'])
    else
        add_project_arguments(['-DNO_DIAGNOSE', '-DNDEBUG'], language: ['c', 'cpp'])
    endif

    enable_profiling = get_option('profiling')
    if enable_profiling
        add_global_arguments(
            [
                '-DTRACY_ENABLE',
                '-DTRACY_DELAYED_INIT',
                '-DTRACY_MANUAL_LIFETIME',
                '-DTRACY_NO_FRAME_IMAGE',
                '-DTRACY_SYMBOL_OFFLINE_RESOLVE',
            ],
            language: ['cpp'],
        )
    endif

    libctru_subproject = subproject(
        'libctru',
        required: true,
        default_options: ['profiling=' + enable_profiling.to_string()],
    )
    libctru = libctru_subproject.get_variable('libctru_dep')

    if build_type.startswith('debug')
        citro3d_lib = 'citro3dd'
    else
        citro3d_lib = 'citro3d'
    endif

    citro3d = declare_dependency(
        include_directories: include_directories(
            get_option('devkitpro') / 'libctru/include',
        ),
        link_args: [
            '-L' + get_option('devkitpro') / 'libctru/lib',
            '-l' + citro3d_lib,
        ],
        dependencies: [libctru],
    )

endif

# Don't use mGBA logging in release build types, only debug and friends
if build_type.startswith('release') or build_type.startswith('minsize')
    add_project_arguments('-DNO_DIAGNOSE', language: ['c', 'cpp'])
elif build_type.startswith('debug')
    add_project_arguments('-DHSM_DEBUG=0', language: ['c', 'cpp'])
endif

# Various helper libraries, subprojects, and scripts common to all builds
fs = import('fs')
tools = subproject('general-tools')
padbin = tools.get_variable('padbin_executable')
bin2s = files('tools/bin2s.py')
call_bin2s = files('tools/callBin2S.py')
if host_machine.system() == 'horizon'
    elf2nro = find_program('elf2nro')
    nacptool = find_program('nacptool')
    call_build_rom = files('tools/switchMakeNRO.py')
endif

# Required to invoke python scripts in a Windows-native build environment
pymod = import('python')
py3 = pymod.find_installation('python3', required: true)

# Windows uses a pre-compiled xxd
if build_machine.system() == 'windows'
    xxd = files('tools/xxd.exe')
else
    xxd = find_program('xxd', required: true)
endif

# Custom warning level flags
add_project_arguments('-Wall', language: ['c', 'cpp'])
add_project_arguments('-Wno-array-bounds', language: ['c', 'cpp'])
add_project_arguments('-Wno-shadow', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused', language: ['c', 'cpp'])

subdir('assets')
subdir('assets/shaders')

if host_machine.system() != 'gba' and host_machine.system() != 'n3ds'
    # 'vita' takes the same dependency branch as switch/horizon below
    #############################
    # Windows, macOS, and Linux #
    #############################

    json = dependency('nlohmann_json', fallback: ['nlohmann_json', 'nlohmann_json_dep'])
    opus = dependency('opus', fallback: ['opus', 'opus_dep'])
    ogg = dependency('ogg', fallback: ['ogg', 'libogg_dep'])
    if host_machine.system() == 'emscripten' or host_machine.system() == 'horizon'
        # Other required dependencies
        openmpt = dependency(
            'openmpt',
            default_options: ['default_library=static'],
            fallback: ['openmpt', 'openmpt_dep'],
        )
        tilengine = dependency(
            'Tilengine',
            default_options: ['default_library=static'],
            fallback: ['Tilengine', 'tilengine_dep'],
        )
        sdl2_dep = dependency(
            'SDL2',
            default_options: ['default_library=static', 'test=false'],
            fallback: ['sdl2', 'sdl2_dep'],
        )
        if host_machine.system() == 'horizon'
            sdl_mixer = dependency(
                'SDL2_mixer',
                default_options: ['default_library=static'],
                fallback: ['sdl2_mixer', 'sdl2_mixer_dep'],
            )
        else
            sdl_mixer = dependency(
                'SDL2_mixer',
                default_options: ['default_library=static'],
                fallback: ['sdl2_mixer', 'sdl2_mixer_dep'],
            )
        endif
        soloud = dependency(
            'SoLoud',
            default_options: ['default_library=static'],
            fallback: ['SoLoud', 'soloud_dep'],
        )
    else
        # Other required dependencies
        openmpt = dependency(
            'openmpt',
            static: true,
            default_options: ['default_library=static'],
            fallback: ['openmpt', 'openmpt_dep'],
        )

        tilengine_default_options = ['default_library=static']

        tilengine = dependency(
            'Tilengine',
            static: true,
            default_options: tilengine_default_options,
            fallback: ['Tilengine', 'tilengine_dep'],
        )

        if host_machine.system() == 'darwin'
            sdl2_dep = subproject(
                'sdl2',
                default_options: [
                    'use_audio_jack=disabled',
                    'default_library=static',
                    'test=false',
                ],
            ).get_variable('sdl2_dep')
            sdl_mixer = dependency(
                'sdl2_mixer',
                static: true,
                default_options: ['default_library=static'],
                fallback: ['sdl2_mixer', 'sdl2_mixer_dep'],
            )
        else
            if not get_option('portmaster')
                sdl2_dep = dependency(
                    'sdl2',
                    static: true,
                    default_options: ['default_library=static', 'test=false'],
                    fallback: ['sdl2', 'sdl2_dep'],
                )
                sdl_mixer = dependency(
                    'sdl2_mixer',
                    static: true,
                    default_options: ['default_library=static'],
                    fallback: ['sdl2_mixer', 'sdl2_mixer_dep'],
                )
            else
                sdl2_dep = dependency(
                    'sdl2',
                    static: false,
                    default_options: ['default_library=shared', 'test=false'],
                    fallback: ['sdl2', 'sdl2_dep'],
                )
                sdl_mixer = dependency(
                    'sdl2_mixer',
                    static: false,
                    default_options: ['default_library=shared'],
                    fallback: ['sdl2_mixer', 'sdl2_mixer_dep'],
                )
            endif
        endif
        soloud = dependency(
            'SoLoud',
            static: true,
            default_options: ['default_library=static'],
            fallback: ['SoLoud', 'soloud_dep'],
        )
    endif

    dependencies = [
        dependency('threads'),
        json,
        ogg,
        openmpt,
        sdl_mixer,
        opus,
        tilengine,
        sdl2_dep,
        soloud,
    ]
    if host_machine.system() == 'windows'
        subproject('curl', default_options: ['default_library=static'])
    endif

    if host_machine.system() != 'horizon' and host_machine.system() != 'vita'
        dependencies += dependency('cpr')
    endif
    if host_machine.system() != 'emscripten' and host_machine.system() != 'horizon' and host_machine.system() != 'vita'
        datachannel = dependency(
            'libdatachannel',
            fallback: ['libdatachannel', 'libdatachannel_dep'],
        )
        dependencies += datachannel
    elif host_machine.system() == 'emscripten'
        cmake = import('cmake')
        datachannel = cmake.subproject('datachannel-wasm')
        dependencies += datachannel.dependency('datachannel-wasm')
    endif

    # Define non-GBA specific preprocessor guards
    if host_machine.system() == 'horizon'
        add_project_arguments('-DSWITCH', language: ['cpp', 'c'])
    elif host_machine.system() == 'vita'
        add_project_arguments('-DVITA', language: ['cpp', 'c'])
    elif get_option('portmaster')
        add_project_arguments('-DPORTMASTER', language: ['cpp', 'c'])
        add_project_arguments('-DGL', language: ['c', 'cpp'])

        dependencies += dependency('glesv2')
    elif host_machine.system() == 'android'
        add_project_arguments('-DANDROID', language : ['cpp', 'c'])
        add_project_arguments('-DGL', language : ['c', 'cpp'])
        add_project_arguments('-DIMGUI_IMPL_OPENGL_ES3', language : ['c', 'cpp'])

        gles3 = meson.get_compiler('c').find_library('GLESv3')
        dependencies += gles3
        
        # Override gl dependency so imgui subproject finds it
        meson.override_dependency('gl', gles3)

        # ImGui Integration
        imgui_proj = subproject('imgui', default_options: ['default_library=static', 'opengl=enabled', 'sdl2=enabled', 'sdl2_renderer=enabled'])
        dependencies += imgui_proj.get_variable('imgui_dep')

    elif host_machine.system() != 'emscripten'
        add_project_arguments('-DPC', language: ['cpp', 'c'])
        add_project_arguments('-DGL', language: ['c', 'cpp'])
        if host_machine.system() == 'windows'
            add_project_arguments('-DWINDOWS', language: ['c', 'cpp'])
        endif

        dependencies += dependency('gl')
        dependencies += dependency('cpr')
    else
        add_project_arguments('-DWEB', language: ['cpp', 'c'])
    endif

    add_project_arguments('-DTE', language: ['c', 'cpp'])

    generated_files = []
    # Assemble bin2s targets
    foreach binfile : binary_files
        filename = fs.name(binfile).split('.')[0]
        output_o = '@0@.bin.o'.format(fs.name(filename))
        output_h = '@0@_bin.h'.format(fs.name(filename))

        if host_machine.system() == 'darwin'
            output_s = '@0@_bin.s'.format(fs.name(filename))
            bin2s_target = custom_target(
                'bin2s_' + fs.name(binfile),
                output: [output_s, output_h],
                command: [
                    py3,
                    call_bin2s,
                    bin2s,
                    binfile,
                    output_h,
                    output_s,
                    'apple',
                    xxd,
                ],
                depend_files: binfile,
            )
        else
            output_cpp = '@0@_bin.cpp'.format(fs.name(filename))
            bin2s_target = custom_target(
                'bin2s_' + fs.name(binfile),
                output: [output_cpp, output_h],
                command: [
                    py3,
                    call_bin2s,
                    bin2s,
                    binfile,
                    output_h,
                    output_cpp,
                    'gcc',
                    xxd,
                ],
                depend_files: binfile,
            )
        endif
        generated_files += bin2s_target

    endforeach

    if meson.get_compiler('c').get_id() != 'clang'
        link_args = [
            '-static-libgcc',
            '-static-libstdc++',
        ]
    else
        link_args = []
    endif

    name_suffix = ''

    if host_machine.system() == 'emscripten'
        link_args += '-sAUDIO_WORKLET=1' # Avoid deprecated features in Chrome, Safari, etc.
        link_args += '-sWASM_WORKERS=1' # New threading model
        link_args += '-sSTACK_SIZE=131072' # musl default; gcc default crashes
        link_args += '--emrun'
        link_args += '-sUSE_SDL=2'
        link_args += '-sUSE_SDL_MIXER=2'
        link_args += '-sASYNCIFY' # Necessary to feed JS event pump
        link_args += '-lidbfs.js' # Game doesn't boot without it
        link_args += '-sALLOW_MEMORY_GROWTH=0' # Necessary for mobile webkit
        link_args += '-sINITIAL_MEMORY=67108864' # Reserve 64 MB of heap (default of 16MB works everywhere but let's be safe)
        link_args += '-sMIN_SAFARI_VERSION=150000' # Necessary for pthreads; not relevant on other browsers
        link_args += '--preload-file'
        link_args += 'assets@/assets'
        name_suffix = 'html'
    elif host_machine.system() == 'horizon'
        link_args += '-specs=/opt/devkitpro/libnx/switch.specs'
        name_suffix = 'elf'
    elif host_machine.system() == 'windows'
        link_args += '-Wl,-Bstatic,--whole-archive'
        link_args += '-lwinpthread'
        link_args += '-Wl,--no-whole-archive'
    elif host_machine.system() == 'darwin'
        link_args += '-mmacosx-version-min=10.15'
    elif host_machine.system() == 'vita'
        # Required by vita-elf-create to resolve relocations against
        # VitaSDK's stub libraries.
        link_args += '-Wl,-q'
    endif

    # The final Apotris compile target
    if host_machine.system() == 'emscripten'
        binary = executable(
            name,
            generated_files + sources,
            include_directories: includes,
            dependencies: dependencies,
            name_suffix: name_suffix,
            link_args: link_args,
            win_subsystem: 'windows',
            install_rpath: 'lib',
        )
    elif host_machine.system() == 'horizon'
        binary = executable(
            name,
            generated_files + sources,
            include_directories: includes,
            dependencies: dependencies,
            link_args: link_args,
            install_rpath: 'lib',
        )
        nro = custom_target(
            'Build romfs and final NRO for Apotris on Nintendo Switch',
            output: 'Apotris.nro',
            command: [
                py3,
                call_build_rom,
                elf2nro,
                nacptool,
                icon_file,
                binary,
                meson.project_version(),
            ],
            depends: binary,
            build_by_default: true,
        )
    elif host_machine.system() == 'vita'
        binary = executable(
            name,
            generated_files + sources,
            include_directories: includes,
            dependencies: dependencies,
            link_args: link_args,
            install_rpath: 'lib',
        )
        # UNVERIFIED: mirrors the switch/NRO custom_target pattern above, but
        # this has not been run against a real VitaSDK toolchain yet. It
        # expects vita-elf-create, vita-make-fself, vita-mksfoex and
        # vita-pack-vpk (all part of vitasdk's vita-toolchain) to be on PATH.
        vita_elf_create = find_program('vita-elf-create', required: false)
        vita_make_fself = find_program('vita-make-fself', required: false)
        vita_mksfoex = find_program('vita-mksfoex', required: false)
        vita_pack_vpk = find_program('vita-pack-vpk', required: false)
        call_build_vpk = files('tools/vitaMakeVPK.py')
        vpk = custom_target(
            'Build eboot/param.sfo and final VPK for Apotris on PS Vita',
            output: 'Apotris.vpk',
            command: [
                py3,
                call_build_vpk,
                vita_elf_create,
                vita_make_fself,
                vita_mksfoex,
                vita_pack_vpk,
                binary,
                meson.project_version(),
                meson.current_build_dir(),
            ],
            depends: binary,
            build_by_default: true,
        )
    elif host_machine.system() == 'android'
        binary = shared_library(
            'main',
            generated_files + sources,
            include_directories : includes,
            dependencies : dependencies,
            link_args : link_args,
            name_prefix : 'lib',
        )

        # Android APK packaging
        deploy_script = files('tools/android_deploy.py')

        apk = custom_target(
            'android-apk',
            output : 'Apotris.apk',
            command : [
                py3, deploy_script,
                '--build-dir', meson.current_build_dir(),
                '--source-dir', meson.current_source_dir(),
                '--buildtype', get_option('buildtype'),
                '--libmain', binary,
                '--output', '@OUTPUT@',
                '--compiler', meson.get_compiler('cpp').cmd_array(),
                '--abi', host_machine.cpu_family() == 'aarch64' ? 'arm64-v8a' : host_machine.cpu_family(),
            ],
            depends : binary,
            build_by_default : true,
        )

        alias_target('apk', apk)
    else
        binary = executable(
            name,
            generated_files + sources,
            include_directories: includes,
            dependencies: dependencies,
            link_args: link_args,
            win_subsystem: 'windows',
            install_rpath: 'lib',
            install: true,
        )
        if host_machine.system() == 'darwin'
            install_data('dist' / 'Apotris.icns', install_dir: 'Contents/Resources')
            install_data('dist' / 'Apotris.bash', install_dir: 'Contents/MacOS')
            install_data('dist' / 'Info.plist', install_dir: 'Contents')
            install_subdir(audio_dir, install_dir: 'Contents/Resources')
            install_subdir('license', install_dir: 'Contents/Resources')
        elif host_machine.system() == 'linux'
            install_subdir(audio_dir, install_dir: '.')
            install_subdir(shaders_dir, install_dir: '.')
            install_subdir('license', install_dir: '.')
            install_data('dist' / 'Apotris.desktop', install_dir: 'share/applications')
            install_data('sprites' / 'logo512.png', install_dir: 'share/icons/hicolor/512x512/apps', rename: 'Apotris.png')
        else
            install_subdir(audio_dir, install_dir: '.')
            install_subdir(shaders_dir, install_dir: '.')
            install_subdir('license', install_dir: '.')
        endif
    endif
elif host_machine.system() == 'n3ds'
    #####################
    # Nintendo 3DS port #
    #####################

    sources += files(
        src_dir / 'liba_n3ds.cpp',
        src_dir / 'n3ds/audio.cpp',
        src_dir / 'n3ds/networking.cpp',
        src_dir / 'n3ds/renderer.cpp',
    )

    # Enable all 3DS-specific code
    add_project_arguments('-DN3DS', language: ['c', 'cpp'])

    ctru_error_handler = static_library(
        'ctru_error_handler',
        files(
            src_dir / 'n3ds/ctru_assert.c',
            src_dir / 'n3ds/ctru_debug_stderr.c',
            src_dir / 'n3ds/ctru_terminate.cpp',
        ),
        cpp_args: ['-fexceptions', '-D_GNU_SOURCE'],
    )

    dependencies = [libctru, citro3d]

    if enable_debug_ui
        tilengine = dependency(
            'Tilengine',
            static: true,
            default_options: ['default_library=static'],
            fallback: ['Tilengine', 'tilengine_dep'],
        )
        dependencies += tilengine

        subdir('source/n3ds/imgui')
        dependencies += imgui_3ds_dep
        dependencies += imgui_dep
    endif

    datachannel = dependency('libdatachannel', static: true, default_options: ['default_library=static'], fallback: ['libdatachannel', 'libdatachannel_dep'])
    dependencies += datachannel

    json = dependency('nlohmann_json', static: true, fallback: ['nlohmann_json', 'nlohmann_json_dep'])
    dependencies += json

    libxmp = dependency('libxmp', static: true)
    dependencies += libxmp

    libvorbisidec = dependency('vorbisidec', static : true)
    dependencies += libvorbisidec

    libmpg123 = dependency('libmpg123', static : true)
    dependencies += libmpg123

    # Build opus ourselves for better performance with fixed-point on 3DS.
    libopus_subproject = subproject(
        'opus',
        required: true,
        default_options: [
            'fixed-point=true',
            'float-api=false',
            'rtcd=disabled',
            'extra-programs=disabled',
            'tests=disabled',
            'docs=disabled',
        ],
    )
    libopusfile_subproject = subproject(
        'opusfile',
        required: true,
        default_options: [
            'fixed-point=true',
            'float-api=false',
        ],
    )
    libopusfile = libopusfile_subproject.get_variable('opusfile_dep')
    dependencies += libopusfile

    if enable_profiling
        tracy = dependency('tracy', static: true, default_options: ['default_library=static']) #, fallback : ['Tilengine', 'tilengine_dep'])
        dependencies += tracy
    endif

    generated_files = []

    # Assemble bin2s targets
    foreach binfile : binary_files
        filename = fs.name(binfile).split('.')[0]
        output_o = '@0@.bin.o'.format(fs.name(filename))
        output_h = '@0@_bin.h'.format(fs.name(filename))
        output_s = '@0@_bin.s'.format(fs.name(filename))

        bin2s_target = custom_target(
            'bin2s_' + fs.name(binfile),
            output: [output_s, output_h],
            command: [
                py3,
                call_bin2s,
                bin2s,
                binfile,
                output_h,
                output_s,
                'gba',
                xxd,
            ],
            depend_files: binfile,
        )

        generated_files += bin2s_target
    endforeach

    picasso_wrapper = files('tools/picasso_wrapper.py')

    shader_shbin = custom_target(
        input: [
            src_dir / 'n3ds/shader_sprite.v.pica',
            src_dir / 'n3ds/shader_bg.v.pica',
            src_dir / 'n3ds/shader_bg.g.pica',
            src_dir / 'n3ds/shader_gradient.v.pica',
            src_dir / 'n3ds/shader_bg_mosaic_tex.v.pica',
            src_dir / 'n3ds/shader_simple_tex.v.pica',
        ],
        output: [
            'n3ds_shader.shbin',
            'n3ds_shader.dvle.h',
        ],
        command: [
            py3,
            picasso_wrapper,
            '-o', '@OUTPUT0@',
            '@INPUT@',
        ],
    )
    shader_shbin_bin2s = custom_target(
        'n3ds_shader_bin2s',
        input: shader_shbin[0],
        output: ['n3ds_shader.shbin.s', 'n3ds_shader.shbin.h'],
        command: [
            py3,
            call_bin2s,
            bin2s,
            '@INPUT@',
            '@OUTPUT1@',
            '@OUTPUT0@',
            'gba',
            xxd,
        ],
    )

    generated_files += [
        shader_shbin_bin2s,
    ]

    elf = executable(
        name,
        generated_files + sources,
        include_directories: includes,
        dependencies: dependencies,
        link_args: [
            '-Wl,-Map=' + name + '.map'
        ],
        link_whole: [ctru_error_handler],
        name_suffix: 'elf',
    )

    smdh_name = name
    if enable_debug_ui
        smdh_name += ' (debug)'
    endif
    if enable_profiling
        smdh_name += ' (profiling)'
    endif

    smdh_longname = 'Apotris multiplatform block stacking game'
    smdh_author = 'akouzoukos, 3DS port by Alvin'

    smdh = custom_target(
        input: [
            src_dir / 'n3ds/icon_48.png',
        ],
        output: name + '.smdh',
        command: [
            'smdhtool',
            '--create', smdh_name,
            smdh_longname,
            smdh_author,
            '@INPUT0@',
            '@OUTPUT@',
            # '@INPUT1@'
        ],
    )

    if enable_profiling
        output_name = name + '-prof'
    else
        output_name = name
    endif

    rom_3dsx = custom_target(
        output: output_name + '.3dsx',
        input: [elf, smdh],
        command: [
            '3dsxtool',
            '@INPUT0@',
            '@OUTPUT@',
            '--smdh=@INPUT1@',
            #'--romfs=./romfs',
        ],
        build_by_default: true,
    )

    alias_target('3dsx', rom_3dsx)

    cia_banner = custom_target(
        output: 'cia.bnr',
        input: [
            src_dir / 'n3ds/banner_image.png',
            src_dir / '../assets/levelUP.wav',
        ],
        command: [
            'bannertool',
            'makebanner',
            '--image', '@INPUT0@',
            '--audio', '@INPUT1@',
            '--output', '@OUTPUT@',
        ],
    )

    cia_smdh = custom_target(
        output: 'cia.smdh',
        input: [
            src_dir / 'n3ds/icon_48.png',
        ],
        command: [
            'bannertool',
            'makesmdh',
            '--shorttitle', smdh_name,
            '--longtitle', smdh_longname,
            '--publisher', smdh_author,
            '--icon', '@INPUT0@',
            '--output', '@OUTPUT@',
            '--flags', 'visible,recordusage,savedata',
        ],
    )

    version_array = meson.project_version().split('.')

    rom_cia = custom_target(
        output: output_name + '.cia',
        input: [
            src_dir / 'n3ds/cia.rsf',
            elf,
            cia_smdh,
            cia_banner,
        ],
        command: [
            'makerom',
            '-rsf', '@INPUT0@',
            '-f', 'cia',
            '-o', '@OUTPUT@',
            '-elf', '@INPUT1@',
            '-icon', '@INPUT2@',
            '-banner', '@INPUT3@',
            '-major', version_array[0],
            '-minor', version_array[1],
            '-micro', version_array[2],
        ],
    )

    alias_target('cia', rom_cia)

else
    ##########################################
    # GameBoy-Advance-specific configuration #
    ##########################################

    # Check Wonderful Toolchain and Eden Toolchain
    compiler = meson.get_compiler('c')
    cc_version_identifier = cc_ver.stdout().to_lower()
    have_eden = cc_version_identifier.contains('wonderful') or cc_version_identifier.contains('eden')
    # Check devkitARM
    have_devkitarm = cc_version_identifier.contains('devkitarm')

    # Gather link arguments and per-toolchain override flags and sources
    link_args = []
    sync_none = ['-specs=sync-none.specs']

    # Use correct toolchain .specs to start
    if compiler.get_id().contains('gcc') and cc.version().version_compare('> 13')
        sources += files('source/stubs.s')
    endif
    if have_devkitarm # devkitARM
        link_args += sync_none
    elif not have_eden # e.g. generic arm.com
        link_args += ['-specs=nosys.specs']
        sources += files('source/stubs.s')
    else # wf and eden
        link_args += sync_none
        link_args += ['-specs=picolibc.specs', '-lnosys']
        sources += files('source/stubs.c')
    endif

    link_mb_args = link_args + ['-flto=auto']

    dependencies = [
        dependency('minrt'),
        dependency('libseven'),
        dependency('libutil'),
        dependency('libsavgba'),
        dependency('libtonc'),
        dependency('agbabi'),
        dependency('posprintf'),
        dependency('gbfs'),
        dependency('maxmod'),
        dependency('gba-link-connection'),
        dependency('gba-rumble'),
    ]

    '''
    dependencies_multi = [
        dependency('minrt-multiboot'),
        dependency('libseven'),
        dependency('libutil'),
        dependency('libtonc'),
        dependency('agbabi'),
        dependency('posprintf'),
        dependency('gba-link-connection'),
    ]
    '''

    # Suppress LinkWireless header warnings
    add_project_arguments('-Wno-format', language: 'cpp')
    add_project_arguments('-Wno-format', language: 'c')

    # Enable all GBA-specific code
    add_project_arguments('-DGBA', language: 'cpp')
    add_project_arguments('-DGBA', language: 'c')

    # Disable mGBA printf on release builds
    if get_option('buildtype').startswith('release')
        add_project_arguments('-DNO_DIAGNOSE', language: 'cpp')
        add_project_arguments('-DNO_DIAGNOSE', language: 'c')
    endif

    # Get build-machine native tools
    gba_tools = subproject('gba-tools')
    gbfs = gba_tools.get_variable('gbfs_executable')
    gbafix = find_program('gbafix-py', required: true)
    makerom = find_program('makerom', required: true)
    makebase = files('tools/makeBase.py')
    subproject('mmutil')
    mmutil = find_program('mmutil')
    subproject('agbpack')
    agbpack = find_program('agbpack', required: true)

    # References to python helper scripts
    buildSoundbank = files('tools/buildSoundbank.py')
    gbabuild = files('tools/gbaBuild.py')
    pocket_patch = files('tools/pocket.py')
    #multibootbuild = files('tools/multibootBuild.py')

    soundbank_files = audio_files
    soundbank_bin = 'soundbank.bin'
    soundbank_header = 'soundbank.h'
    sound_effects = files('tools/sound_effects.txt')

    generated_files = []
    soundbank_target = custom_target(
        'soundbank',
        output: [
            soundbank_bin,
            soundbank_header,
            effect_locations_file,
            'audio.gbfs',
        ],
        command: [
            py3,
            buildSoundbank,
            'default',
            mmutil,
            gbfs,
            sound_effects,
            soundbank_files,
        ],
        depend_files: soundbank_files,
    )
    generated_files += soundbank_target

    # Assemble bin2s targets
    foreach binfile : binary_files
        filename = fs.name(binfile).split('.')[0]
        output_o = '@0@.bin.o'.format(fs.name(filename))
        output_h = '@0@_bin.h'.format(fs.name(filename))
        output_s = '@0@_bin.s'.format(fs.name(filename))

        bin2s_target = custom_target(
            'bin2s_' + fs.name(binfile),
            output: [output_s, output_h],
            command: [
                py3,
                call_bin2s,
                bin2s,
                binfile,
                output_h,
                output_s,
                'gba',
                xxd,
            ],
            depend_files: binfile,
        )

        generated_files += bin2s_target
    endforeach

    subdir('assets/multiboot')
    # Assemble multiboot audio bin2s targets
    foreach binfile : multiboot_audio_files
        filename = fs.name(binfile).split('.')[0]
        output_o = '@0@.bin.o'.format(fs.name(filename))
        output_h = '@0@_bin.h'.format(fs.name(filename))
        output_s = '@0@_bin.s'.format(fs.name(filename))

        bin2s_target = custom_target(
            'bin2s_' + fs.name(binfile),
            output: [output_s, output_h],
            command: [
                py3,
                call_bin2s,
                bin2s,
                binfile,
                output_h,
                output_s,
                'gba',
                xxd,
            ],
            depend_files: binfile,
        )

        generated_files += bin2s_target
    endforeach

    ##########################################
    # Multiboot-specific build configuration #
    ##########################################

    '''
    # The base multiboot elf binary
    multi = executable(
        name + '-multi',
        sources + generated_files + 'source/vgm.cpp',
        link_args: link_mb_args,
        cpp_args: ['-flto', '-DMULTIBOOT', '-Oz'],
        c_args: ['-flto', '-Oz'],
        include_directories: includes,
        dependencies: dependencies_multi,
        name_suffix: 'elf',
    )

    # The base multiboot ROM with no audio, unpadded
    multi_base = custom_target(
        name + '-multi-base',
        input: multi,
        output: name + '-multi-base.gba',
        command: [agbpack, '@INPUT@', '@OUTPUT@'],
    )

    # The base multiboot ROM with no audio, padded and fixed
    multi_rom = custom_target(
        name + '-multi',
        input: multi_base,
        output: name + '-multi.gba',
        command: [multibootbuild, padbin, '0x10', '@INPUT@', '@OUTPUT@', gbafix],
    )

    output_o = 'Multiboot.o'
    output_h = 'Multiboot.h'
    output_s = 'Multiboot.s'

    embedded_multiboot = custom_target(
        'bin2s_multiboot',
        output: [output_s, output_h],
        command: [py3, call_bin2s, bin2s, multi_rom, output_h, output_s, 'gba', xxd],
        depends: multi_rom,
    )

    generated_files += embedded_multiboot
    '''

    # Flag enables bootleg save support
    rom_save = ['-DENABLE_ROM_SAVE']

    ##########################################
    # Base GBA ROM build configuration steps #
    ##########################################

    # The base elf binary
    elf = executable(
        name,
        sources + generated_files,
        cpp_args: rom_save,
        c_args: rom_save,
        link_args: link_args,
        include_directories: includes,
        dependencies: dependencies,
        name_suffix: 'elf',
    )

    # The base GBA ROM with no audio
    rom_base = custom_target(
        name + '-base',
        input: elf,
        output: name + '-base.gba',
        command: [py3, makebase, makerom, '@INPUT@', '@OUTPUT@', gbafix],
    )

    # The final GBA ROM with audio
    rom = custom_target(
        'final ROM with audio',
        output: name + '.gba',
        input: rom_base,
        command: [
            py3,
            gbabuild,
            padbin,
            '256',
            '@INPUT@',
            'audio.gbfs',
            '0x400000',
            '@OUTPUT@',
            gbafix,
        ],
        build_by_default: true,
    )

    #########################################
    # EZFlash Air build configuration steps #
    #########################################

    # The base elf binary without rom save support for EZFlash Air
    elf_ezf_air = executable(
        name + '-Air',
        sources + generated_files,
        link_args: link_args,
        include_directories: includes,
        dependencies: dependencies,
        name_suffix: 'elf',
        build_by_default: false,
    )

    # The base EZFlash Air GBA ROM with no audio
    rom_base_air = custom_target(
        name + '-Air-base',
        input: elf_ezf_air,
        output: name + '-Air-base.gba',
        command: [py3, makebase, makerom, '@INPUT@', '@OUTPUT@', gbafix],
        build_by_default: false,
    )

    # The final EZFlash Air GBA ROM with audio
    rom_air = custom_target(
        'final EZFlash Air ROM with audio',
        output: name + '-Air.gba',
        input: rom_base_air,
        command: [
            py3,
            gbabuild,
            padbin,
            '256',
            '@INPUT@',
            'audio.gbfs',
            '0x400000',
            '@OUTPUT@',
            gbafix,
        ],
        build_by_default: false,
    )

    #############################################
    # Analogue Pocket build configuration steps #
    #############################################

    # Analogue Pocket rumble-patched ROM (masquerades as DRILL DOZER)
    pocket = custom_target(
        'patched ROM for Analogue Pocket rumble support',
        output: name + '-Pocket.gba',
        input: rom,
        command: [py3, pocket_patch, gbafix, '@INPUT@', '@OUTPUT@'],
        build_by_default: false,
    )

    ##########################################
    # UPS patch generator for final releases #
    ##########################################

    # A list that pairs [MODIFIED_ROM_TARGET, ITS_BASE_ROM_TARGET, 'output-suffix']
    patches_to_generate = [
        [pocket, rom, 'Pocket'],
        [rom_air, rom, 'Air'],
        [rom_base_air, rom_base, 'Air-base'],
    ]
    all_ups_targets = []

    # Go binary
    golang = find_program('go', required: false)

    if golang.found()
        message('Go found, defining .ups patch file generation targets.')
        # The ups patcher tool, downloaded from the internet and built in-place
        ups_cmd = [golang, 'run', 'github.com/rameshvarun/ups@latest']

        # Import optional patch targets for final release
        subdir('patches')
    else
        message('Go NOT found, skipping UPS patch generation target.')
    endif
endif
