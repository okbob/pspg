project('pspg', ['c'], version: '5.8.16', meson_version: '>= 1.5.0')

build_args = [
  '-DPROJECT_NAME=' + meson.project_name(),
  '-DPROJECT_VERSION=' + meson.project_version(),
  '-DCOMPILE_MENU',
]

cc = meson.get_compiler('c')
fs = import('fs')
is_windows = host_machine.system() == 'windows'
prefix = get_option('dep_prefix')
shlibs = get_option('shlibs')

delayload = false
foreach e : get_option('c_link_args')
  if e.to_lower().contains('delayload')
    delayload = true
  endif
endforeach

curses_backend = get_option('curses_backend')
if curses_backend == 'auto'
  curses_backend = is_windows ? 'pdcurses' : 'curses'
endif

want_static = shlibs != 'all'
want_full_static = shlibs == 'no'

message('curses backend: ' + curses_backend)
message('shared library mode: ' + shlibs)
message('delayload: ' + delayload.to_string())

project_deps = []
link_args = []
project_includes = [
  include_directories('src'),
  include_directories('src/platform'),
]

cmake_prefix = fs.is_absolute(prefix) ? prefix : meson.current_source_dir() / prefix

if is_windows
  project_includes += [
    include_directories('src/platform/compat/windows'),
    include_directories(prefix / 'include') # vcpkg_installed/triplet/include
  ]
  if cc.get_argument_syntax() == 'msvc'
    project_deps += dependency('getopt', method: 'cmake', required: true, static: want_static,
                               modules: 'getopt::getopt_shared',
                               cmake_args: '-DCMAKE_PREFIX_PATH=' + cmake_prefix)
  endif
  build_args += [
    '-DGWINSZ_IN_SYS_IOCTL',
    '-Dssize_t=ptrdiff_t',
  ]
endif

curses = dependency([curses_backend, 'unofficial-pdcurses'], required: not is_windows, static: want_static,
                        modules: 'unofficial::pdcurses::pdcurses',
                        cmake_args: '-DCMAKE_PREFIX_PATH=' + cmake_prefix)
if curses.found()
  curses_libs = curses.get_variable(default_value: '', cmake: 'PACKAGE_LIBRARIES', pkgconfig : 'libdir')
  if curses_libs == ''
    message('meson fails to properly parse unofficial::pdcurses::pdcurses imported CMake target of "UNKNOWN" type')
    if get_option('buildtype') == 'debug'
      curses_lib_dir = cmake_prefix / 'debug'
    endif
    curses_lib_dir = curses_lib_dir / 'lib'
    project_deps += cc.find_library('pdcurses', dirs: curses_lib_dir)
  endif
else # msys2 fallback
    curses = cc.find_library('pdcurses_wincon')
endif

curses_name = curses.name()
if curses_name.contains('pdcurses')
  # unofficial-pdcurses with MSVC, pdcurses_wincon with msys2
  curses_name = 'pdcurses'
  build_args += [
    '-DPDCURSES',
    '-DPDC_WIDE',
    '-DPDC_FORCE_UTF8',
    '-DHAVE_NCURSESW',
  ]
endif
message(curses_name)
conf = configuration_data()
panel = cc.find_library('panel', required: curses_name != 'pdcurses')
math = cc.find_library('m', required: not is_windows)

have_kqueue = cc.has_function('kqueue', prefix : '#include <sys/event.h>')
if have_kqueue
  build_args += '-DHAVE_KQUEUE'
endif

if ['ncursesw', 'pdcurses'].contains(curses_name)
  message('enabling wide character support')
  build_args += '-DHAVE_NCURSESW'
endif

curses_inc = prefix / 'include' / curses_name
if curses.type_name() == 'pkgconfig' # won't work with find_library
  curses_inc = curses.get_variable(pkgconfig: 'includedir')
endif
if fs.is_dir(curses_inc)
  message('Adding curses include directory: ' + curses_inc)
  project_includes += include_directories(curses_inc)
endif

readline = dependency(['readline', 'unofficial-readline-win32'], 
  static: want_static, required: false,
  modules: 'unofficial::readline-win32::readline', 
  cmake_args: '-DCMAKE_PREFIX_PATH=' + cmake_prefix
)
if readline.found()
  readline_inc = readline.get_variable(pkgconfig: 'includedir', default_value: prefix / 'include') / 'readline'
  if fs.is_dir(readline_inc)
    message('Adding readline include directory: ' + readline_inc)
    project_includes += include_directories(readline_inc)
  endif
endif

check_headers = [
  ['ncursesw.h', '-DHAVE_NCURSESW_H'],
  ['ncurses.h', '-DHAVE_NCURSES_H'],
  ['curses.h', '-DHAVE_CURSES_H'],
  ['panel.h', '-DHAVE_PANEL_H'],
  ['readline.h', ['-DHAVE_LIBREADLINE', '-DHAVE_READLINE_H']],
  ['history.h', ['-DHAVE_READLINE_HISTORY', '-DHAVE_HISTORY_H']]
]

if not have_kqueue # FreeBSD has both, prefer kqueue
  check_headers += [['sys/inotify.h', '-DHAVE_INOTIFY']]
endif

foreach h : check_headers
  if cc.has_header(h.get(0), include_directories: project_includes)
    build_args += h.get(1)
  endif
endforeach

pg = dependency('libpq', required: false, static: want_full_static)
if not pg.found()
  pgroot = get_option('pgroot')
  if pgroot != ''
    pg = declare_dependency(
      dependencies : cc.find_library('pq', dirs: pgroot / 'lib', required: false),
      include_directories : include_directories(pgroot / 'include')
    )
  else
    message('PostgreSQL library not found; libpq support will be disabled')
    message('Set the pgroot option to specify a PostgreSQL installation for libpq support')
  endif
endif
if pg.found()
  build_args += '-DHAVE_POSTGRESQL'
  project_deps += cc.find_library('bsd', required: host_machine.system() == 'haiku') # getpass
endif

sources = [
  'src/args.c',
  'src/bscommands.c',
  'src/commands.c',
  'src/config.c',
  'src/export.c',
  'src/infra.c',
  'src/inputs.c',
  'src/linebuffer.c',
  'src/menu.c',
  'src/pgclient.c',
  'src/pretty-csv.c',
  'src/print.c',
  'src/pspg.c',
  'src/readline.c',
  'src/sort.c',
  'src/st_menu.c',
  'src/st_menu_styles.c',
  'src/string.c',
  'src/table.c',
  'src/theme_loader.c',
  'src/themes.c',
  'src/unicode.c'
]

if is_windows
  sources += files('src/platform/windows.c')
  if delayload
    sources += files('src/platform/dl_reaper.c')
    project_deps += [ cc.find_library('delayimp'), cc.find_library('ntdll') ]
  endif

  project_deps += cc.find_library('ws2_32')
  if curses.name() == 'pdcurses_wincon'
    project_deps += cc.find_library('winmm')
  endif
  if cc.get_argument_syntax() == 'gcc' and not cc.has_function('clock_gettime')
    project_deps += cc.find_library('winpthread')
  endif
endif

if want_static and curses_backend == 'ncursesw'
  build_args += '-DNCURSES_STATIC'
endif

if want_static and cc.get_argument_syntax() == 'gcc'
  build_args += ['-ffunction-sections', '-fdata-sections']
  link_args += ['-static', '-Wl,--gc-sections']
endif

project_target = executable(
  meson.project_name(),
  sources,
  dependencies: project_deps + [pg, readline, curses, panel, math],
  install : true,
  c_args : build_args,
  include_directories: project_includes,
  link_args: link_args,
)
