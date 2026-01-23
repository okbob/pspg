project('pspg', ['c'], version: '5.8.15')

build_args = [
  '-DPROJECT_NAME=' + meson.project_name(),
  '-DPROJECT_VERSION=' + meson.project_version(),
]

cc = meson.get_compiler('c')

panel = cc.find_library('panelw')
curses = dependency('curses')
math = cc.find_library('m')

message(curses.name())

conf = configuration_data()

if curses.name() == 'ncursesw'
  message('detect ncursesw')
  build_args += '-DHAVE_NCURSESW'
endif

build_args += '-DCOMPILE_MENU'

check_headers = [
  ['ncursesw/menu.h', '-DHAVE_NCURSESW_MENU_H'],
  ['ncurses/menu.h', '-DHAVE_NCURSES_MENU_H'],
  ['menu.h', '-DHAVE_MENU_H'],
  ['ncursesw/curses.h', '-DHAVE_NCURSESW_CURSES_H'],
  ['ncursesw.h', '-DHAVE_NCURSESW_H'],
  ['ncurses/curses.h', '-DHAVE_NCURSES_CURSES_H'],
  ['ncurses.h', '-DHAVE_NCURSES_H'],
  ['curses.h', '-DHAVE_CURSES_H'],
  ['ncursesw/panel.h', '-DHAVE_NCURSESW_PANEL_H'],
  ['ncurses/panel.h', '-DHAVE_NCURSES_PANEL_H'],
  ['panel.h', '-DHAVE_PANEL_H']
]

foreach h : check_headers
  if cc.has_header(h.get(0))
    build_args += h.get(1)
  endif
endforeach

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

project_target = executable(
  meson.project_name(),
  sources,
  dependencies: [ curses, panel, math ],
  install : true,
  c_args : build_args
)
