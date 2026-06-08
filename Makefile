PREFIX = /usr/local
CC = cc
CFLAGS = -Wall -Wextra -O2 -Wno-sign-compare -I/usr/include/taglib
LDFLAGS = -lpthread -lm -ldl -ltag_c -ltag -lz

OBJ = rudis.o miniaudio.o

rudis: $(OBJ) config.h miniaudio.h
	$(CC) $(CFLAGS) -o rudis $(OBJ) $(LDFLAGS)

src/miniaudio.o: miniaudio.c miniaudio.h
	$(CC) $(CFLAGS) -c miniaudio.c -o miniaudio.o

src/rudis.o: rudis.c config.h miniaudio.h
	$(CC) $(CFLAGS) -c rudis.c -o rudis.o

install: rudis
	install -Dm755 rudis $(DESTDIR)$(PREFIX)/bin/rudis
	install -Dm644 rudis.1 $(DESTDIR)$(PREFIX)/share/man/man1/rudis.1
	install -Dm644 completion.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/rudis
	install -Dm644 completion.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/rudis.fish

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/rudis
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/rudis.1
	rm -f $(DESTDIR)$(PREFIX)/share/bash-completion/completions/rudis
	rm -f $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/rudis.fish

clean:
	rm -f rudis *.o

.PHONY: install uninstall clean
