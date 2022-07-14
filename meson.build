project('eyeq', 'c', subproject_dir: 'lib', default_options: ['c_std=gnu99'])

includes = []
c_args = ['-std=c11', '-D_GNU_SOURCE']
link_args = []
library_link_args = []
library_dependencies = []

library_inc = [ 'include', 'lib/nanopb/include' ]
library_sources = files([
	'src/client/client.c',
	'src/server/crc32.c',
	'src/server/server.c',
	'src/server/store/store.c',
	'src/server/stream/stream.c',
	'src/proto/samples.pb.c',
])

if get_option('builtin_nanopb')
	library_sources += files([
		'lib/nanopb/src/pb_common.c',
		'lib/nanopb/src/pb_encode.c',
		'lib/nanopb/src/pb_decode.c',
	])
endif

eyeq_lib_static = static_library(
	'eyeq',
	library_sources,
	c_args: c_args,
	include_directories : library_inc,
	dependencies: library_dependencies,
	link_args: library_link_args,
	install: true)

eyeq_lib_static_dep = declare_dependency(
	include_directories: library_inc,
	dependencies: library_dependencies,
	link_with : eyeq_lib_static,
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : true)

dependencies = []
dependencies += eyeq_lib_static_dep
dependencies += dependency('libzmq', version: '>4.0')
dependencies += m_dep

server_sources = files([
	'src/server/main.c',
])

client_sources = files([
	'src/client/main.c',
	'src/client/eyeq_slash.c',
	'src/client/filters.c',
])

store_test_sources = files([
    'src/server/store/test_store.c',
])

client_test_sources = files([
    'src/client/test_client.c',
])

stream_test_sources = files([
    'src/server/stream/test_stream.c',
])

standalone_sources = files([
])

# nanopb is needed for the tests and standalone executables
if not get_option('builtin_nanopb')
	standalone_sources += files([
		'lib/nanopb/src/pb_common.c',
		'lib/nanopb/src/pb_encode.c',
		'lib/nanopb/src/pb_decode.c',
	])
endif

executable('eyeq-server',
	include_directories : includes,
	dependencies: dependencies,
	sources: server_sources + standalone_sources,
	link_args: link_args,
	c_args: c_args,
	install: true,
)


slash_dep = dependency('slash', fallback: ['slash', 'slash_dep'], required: false)
if slash_dep.found()
	executable('eyeq',
		include_directories : includes,
		dependencies: dependencies + [slash_dep],
		sources: client_sources + standalone_sources,
		link_args: link_args,
		c_args: c_args,
		install: true,
	)
endif

test_dependencies = []
unity_dep = dependency('unity', fallback: ['unity', 'unity_dep'], required: false)


if unity_dep.found()
	test_dependencies += unity_dep

	executable('eyeq-store-test',
	    include_directories: includes,
	    dependencies: dependencies + test_dependencies,
	    sources: store_test_sources + standalone_sources,
	    c_args: c_args)

	executable('eyeq-client-test',
	    include_directories: includes,
	    dependencies: dependencies + test_dependencies,
	    sources: client_test_sources + standalone_sources,
	    c_args: c_args)

	executable('eyeq-stream-test',
	    include_directories: includes,
	    dependencies: dependencies + test_dependencies,
	    sources: stream_test_sources + standalone_sources,
	    c_args: c_args)
endif
