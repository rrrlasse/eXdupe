SRC1=../src/luawrapper.cpp ../src/exdupe.cpp ../src/utilities.cpp ../src/bytebuffer.cpp ../src/libexdupe/*.cpp ../src/libexdupe/skein/*.cpp ../src/trex/trex.cpp ../src/io.cpp
SRC2=$(addprefix -x c , $(wildcard ../src/libexdupe/quicklz/*.c ../src/libexdupe/bzip2/*.c ../src/libexdupe/zlib/*.c ../src/lua/*.c))

all:
	g++ -o exdupe -Wno-strict-aliasing $(STATIC) -Wall -O3 $(SRC1) $(SRC2) -lpthread -DLUA_USE_MKSTEMP

d:
	g++ -o exdupe -Wno-strict-aliasing $(STATIC) -Wall -g $(SRC1) $(SRC2) -lpthread -DLUA_USE_MKSTEMP