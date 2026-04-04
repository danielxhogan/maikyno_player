lib_sources = [
    'mkp.c',
    'read_thread.c',
    'audio.c',
    'decode.c',
    'packet_queue.c',
    'frame_queue.c',
    'clock.c'
]

lib_deps = [
    lavf,
    lavc,
    lswr,
    lavu,
    pipewire,
    threads
]

libmkp = static_library('mkp',
    sources: lib_sources,
    dependencies: lib_deps,
    gnu_symbol_visibility: 'hidden')

subdir('app')
