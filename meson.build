project('maikyno_player', 'c')

lavf = dependency('libavformat', required: true)
lavc = dependency('libavcodec', required: true)
lavu = dependency('libavutil', required: true)
pipewire = dependency('libpipewire-0.3', required: true)

# cc = meson.get_compiler('c')
# libm = cc.find_library('m', required: true)

subdir('src')
