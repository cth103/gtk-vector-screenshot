project('take-vector-screenshot', 'c')

gtk = dependency('gtk+-3.0')
x11 = dependency('x11')
executable(
    'take-vector-screenshot',
    'take-vector-screenshot.c', 'clientwin.c',
    dependencies: [gtk, x11],
    install: true)

shared_library(
    'gtk-vector-screenshot',
    'gtk-vector-screenshot.c', 'clientwin.c',
    dependencies: [gtk, x11],
    link_args: ['-lm'],
    install: true,
    install_dir: '/usr/local/lib/gtk-3.0/modules')
