CXX ?= g++
CXXFLAGS = -Wall -Werror -g -I.
LD = $(CXX)
LDFLAGS = -lpcre


JOBJ = Pcre.o jutil.o
OBJ += $(JOBJ)
JLIB = libjutil.a
LIBS += $(JLIB)

TROBJ = trun.o TestRunner.o 
OBJ += $(TROBJ)
EXES += trun

ETOBJ = egtest.o
OBJ += $(ETOBJ)
EXES += egtest

build: $(LIBS) $(EXES)

trun: $(TROBJ) $(JLIB)
	$(LD) $(TROBJ) -o trun -ljutil $(LDFLAGS) -L.

$(JLIB): $(JOBJ)
	ar rs $(JLIB) $(JOBJ)

egtest: $(ETOBJ)
	$(LD) $(ETOBJ) -o egtest $(LDFLAGS)

clean:
	rm -f $(OBJ) $(EXES) $(JLIB) *~
