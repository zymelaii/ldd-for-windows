all: ldd
.PHONY: all

clean:
	rm ldd.exe
.PHONY: clean

ldd: ldd.cpp
	g++ -o ldd ldd.cpp -lshlwapi
