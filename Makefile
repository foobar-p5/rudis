PREFIX = /usr/local
CC = cc
CFLAGS = -Wall -Wextra -O2 -Wno-sign-compare -I/usr/include/taglib
LDFLAGS = -lpthread -lm -ldl -ltag_c -ltag -lz

OBJ = rudis.o miniaudio.o

rds: $(OBJ) config.h miniaudio.h
	$(CC) $(CFLAGS) -o rds $(OBJ) $(LDFLAGS)

miniaudio.o: miniaudio.c miniaudio.h
	$(CC) $(CFLAGS) -c miniaudio.c -o miniaudio.o

rudis.o: rudis.c config.h miniaudio.h
	$(CC) $(CFLAGS) -c rudis.c -o rudis.o

install: rds
	install -Dm755 rds $(DESTDIR)$(PREFIX)/bin/rds
	install -Dm644 rudis.1 $(DESTDIR)$(PREFIX)/share/man/man1/rds.1
	install -Dm644 completion.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/rds
	install -Dm644 completion.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/rds.fish

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/rds
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/rds.1
	rm -f $(DESTDIR)$(PREFIX)/share/bash-completion/completions/rds
	rm -f $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/rds.fish

clean:
	rm -f rds *.o

.PHONY: install uninstall clean
