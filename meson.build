project('maikyno_player', 'c')

lavf = dependency('libavformat', required: true)
lavc = dependency('libavcodec', required: true)
lavu = dependency('libavutil', required: true)
lswr = dependency('libswresample', required: true)
pipewire = dependency('libpipewire-0.3', required: true)

cc = meson.get_compiler('c')

if host_machine.system() == 'windows'
    threads = declare_dependency()
else
    pthreads = dependency('threads', required: true)
    has_setclock = cc.has_header_symbol(
        'pthread.h',
        'pthread_condattr_setclock',
        dependencies: pthreads)

    threads = declare_dependency(
        dependencies: pthreads,
        compile_args: [
            pthreads.found() ? '-DHAVE_PTHREAD' : '',
            has_setclock ? '-DPTHREAD_HAS_SETCLOCK' : ''
        ]
    )
endif

subdir('src')
