add_library (ed25519
	ed25519-donna-portable.h
	ed25519-hash-custom.h
	ed25519-randombytes-custom.h
	ed25519.h
	ed25519.c)

target_compile_definitions(ed25519 PUBLIC
	-DED25519_CUSTOMHASH
	-DED25519_CUSTOMRNG)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?|AMD64)$")
	target_compile_definitions(ed25519 PUBLIC -DED25519_SSE2)
endif()
