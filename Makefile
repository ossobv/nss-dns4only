# nss-dns4only - disabling AAAA lookup in AF_UNSPEC getaddrinfo()
# License LGPLv3+; Copyright (C) 2020 Walter Doekes, OSSO B.V.
CFLAGS += -Wall -g -O -fPIC
LDFLAGS += -Wl,--as-needed -Wl,--no-undefined
LIBS = -lnss_dns
VERSION = 0.1

_libarchsuffix = /x86_64-linux-gnu
base_prefix =
exec_prefix = $(base_prefix)/usr
base_libdir = $(base_prefix)/lib$(_libarchsuffix)
libdir = $(exec_prefix)/lib$(_libarchsuffix)


.PHONY: all clean dist install example
all: nss-dns4only.so nss-dns4suffix.so
	@echo 'Run "make example" for an explanation.'
clean:
	$(RM) *.o *.so
dist:
	find . -type f '(' -name '*.c' -o -name '*.sym' -o -name Makefile \
	  -o -name '*.rst' -o -name '*.txt' ')' | sed -e 's/^./nss-dns4only/' | \
	  tar -zcf ../nss-dns4only_$(VERSION).orig.tar.gz -C .. -T -
install: all
	install -d $(DESTDIR)$(base_libdir) $(DESTDIR)$(libdir)
	install nss-dns4only.so \
	  $(DESTDIR)$(base_libdir)/libnss_dns4only-$(VERSION).so
	ln -sf libnss_dns4only-$(VERSION).so \
	  $(DESTDIR)$(base_libdir)/libnss_dns4only.so.2
	ln -sf $(base_libdir)/libnss_dns4only.so.2 \
	  $(DESTDIR)$(libdir)/libnss_dns4only.so
	install nss-dns4suffix.so \
	  $(DESTDIR)$(base_libdir)/libnss_dns4suffix-$(VERSION).so
	ln -sf libnss_dns4suffix-$(VERSION).so \
	  $(DESTDIR)$(base_libdir)/libnss_dns4suffix.so.2
	ln -sf $(base_libdir)/libnss_dns4suffix.so.2 \
	  $(DESTDIR)$(libdir)/libnss_dns4suffix.so
	# When building a package, fakeroot will not allow ldconfig to
	# be called. Ignore failure.
	-ldconfig

example:
	@echo 'Installing means inserting "dnsonly [!UNAVAIL=return]" in /etc/nsswitch.conf:'
	@echo
	@echo '  hosts: files dns4only [!UNAVAIL=return] dns'
	@echo
	@echo 'Before and after installing, these should be different:'
	python -c 'from socket import *; print(getaddrinfo("google.com.",443))' || true
	@echo
	@echo '^-- with dns4only enabled: no AAAA will be in the list'
	@echo
	@echo 'Note that setting family=AF_INET or family=AF_INET6 still works.'
	@echo '(Those requests will not use libnss_dns4only.)'
	@echo
	@echo 'libnss_dns4suffix is also provided, more of a test than anything else:'
	@echo "- 'google.com.v4' will then turn up only A records"
	@echo "- 'google.com' will return both A and AAAA as usual"

nss-dns4only.o: nss-dns4only.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $^

nss-dns4suffix.o: nss-dns4only.c
	$(CC) $(CPPFLAGS) -DAS_DNS4SUFFIX=1 $(CFLAGS) -c -o $@ $^

%.so: %.o
	$(LINK.c) -shared -o $@ \
	  -Wl,-soname=$@ -Wl,--version-script=nss-dns4only.sym \
	  $^ $(LIBS)
	objdump -p $@ | grep NEEDED
	nm $@ | grep @@GLIBC_
	ver=$$(nm $@ | sed -e '/@@GLIBC_[0-9]/!d;s/.*@@//' | sort -Vur | \
	  head -n1); \
	  if test "$$ver" != "GLIBC_2.4"; then echo $$ver; rm $@; false; fi
	@echo 'Built $@ with minimal dependencies'; echo
