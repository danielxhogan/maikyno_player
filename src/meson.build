lib_sources = [
    'mkp.c'
]

lib_deps = [
    lavf,
    lavc,
    lavu,
    pipewire
]

libmkp = static_library('mkp',
    sources: lib_sources,
    dependencies: lib_deps)

subdir('app')
