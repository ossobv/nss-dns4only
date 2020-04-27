nss-dns4only
============

*nss-dns4only* is a workaround to disable IPv6 DNS (*AAAA*) lookups when
you don't want them.

It works with *glibc-2.9* or newer through the ``nsswitch.conf`` system:
if applications fail to specify an address family, they will get IPv4
(*A record*) lookups only.

*(This unfortunately also means it does NOT work with musl libc.)*


Why would you want this?
------------------------

Below, I try to verbosely explain the various parameters that affect
IPv4 and IPv6 address resolving (DNS lookups) and how they can conflict:

* DNS and getaddrinfo background information
* Problems A+AAAA lookups can cause
* How nss-dns4only helps (followed by installation information)

**TL;DR:** If you're on a host where you have no IPv6 connectivity, you
can ease the load of your DNS resolvers by using *nss-dns4only*. And,
more importantly, it should also help against bugs like:

* May 2019: `CoreDNS fails to resolve A records under circumstances`_
* March 2020: `CoreDNS autopath+cache cause NXDOMAIN for A or AAAA records`_

In short, those bugs cause intermittent failures in lookups: *sometimes
your curl only gets an IPv6 address, and if your networking stack has no
IPv6, the TCP connection setup then fails.*

When using *nss-dns4only*, your applications will only do *A record*
(IPv4) lookups, unless they explicitly ask for *AAAA* (IPv6). Under the
hood, dual *A+AAAA* lookups are translated to *A* lookups.


DNS and getaddrinfo background information
------------------------------------------

Most client applications nowadays use ``getaddrinfo()`` to translate
domain names into internet addresses: you pass *google.com* and get
``172.217.17.110`` as answer. This returned address can be passed to
the ``connect()`` socket call which in turn sets up a TCP/IP connection.

Since the advent of IPv6, there are both IPv4 (*A*) and IPv6
(*AAAA*) addresses to consider, for the ``AF_INET`` and ``AF_INET6``
*address families* respectively. And due to the way the Domain Name
System (DNS) works, looking up an internet address now requires two
queries: *one for the A record and one for the AAAA record*.
(An aside: it's called *AAAA* because the IPv6 address is four times
as large.) This dual query is handled transparently by the *C libraries*
(``libc`` and ``libresolv``) on your system.

The function signature of ``getaddrinfo()`` is as follows:

.. code-block:: c

    int getaddrinfo(const char *node, const char *service,
                    const struct addrinfo *hints,
                    struct addrinfo **res);

You pass the host name (e.g. *google.com*) in the ``node`` parameter, and get
the *A* and *AAAA* addresses in the return value ``res``.

Furthermore, you can tweak the ``hints`` parameter to specify that want
only IPv4 or IPv6 addresses by setting its ``ai_family`` parameter to
one of ``AF_INET`` or ``AF_INET6``. Or you can set ``AI_ADDRCONFIG`` in
the ``ai_flags`` to specify *that you only want addresses that are
routable on your system.*

That last part is interesting: *if it detects that your network devices
have no IPv6 connectivity, it will not do any AAAA lookups. Any returned
result would be useless to the connect() call anyway.*

In the *GNU C library* (glibc) this ``AI_ADDRCONFIG`` flag defaults to true::

    According to POSIX.1, specifying hints as NULL should cause
    ai_flags to be assumed as 0.  The GNU C library instead assumes a
    value of (AI_V4MAPPED | AI_ADDRCONFIG) for this case, since this
    value is considered an improvement on the specification.

That means that applications that don't specify ``hints`` will get only
addresses they can actually connect to: *this saves an AAAA query
per lookup if your host does not support IPv6.*

**However**, various applications explicitly set the ``hints`` anyway, but
leave the ``ai_flags`` unset. For example: *Python* does, and so does *curl*.
For compatibility with different C libraries, that is a good choice.
*But for systems where there is no IPv6, this can be useless at best,
and cause connection failures at worst.*


Problems A+AAAA lookups can cause
---------------------------------

These problems assume you don't have any IPv6 networking configured on
your host. If do you have IPv6, you should not be looking at
*nss-dns4only*.

Problem: *instead of one DNS lookup, you're always doing two.*

That means:

* Double work for your resolver.

* The total lookup time is always the slowest of the two responses
  (obviously).

And in *Kubernetes*, this problem is aggravated by the domain suffix search.
Andreas Spitzer from *Curve* explains it in `Name Resolution Issue In
CoreDNS`_ (June, 2019):

  If you call *curl https://api.twilio.com* [...] the resolver will try to
  resolve this in the following order:

  - api.twilio.com.default.svc.cluster.local
  - api.twilio.com.svc.cluster.local
  - api.twilio.com.cluster.local
  - api.twilio.com.eu-west-1.compute.internal
  - api.twilio.com

Why *Kubernetes* does this is beyond the scope here, but it does, and
it multiplies the problems we mentioned.

As a shortcut, the *Kubernetes* resolver *CoreDNS* has been adjusted
to immediately respond with the *api.twilio.com* response *as
response to the first lookup*. (Through the *autopath* plugin, possibly
in conjunction with the *cache* plugin.) **However, this does not
consistently work.**

If both the *A* and *AAAA* lookup of
*api.twilio.com.default.svc.cluster.local* get a ``NXDOMAIN`` response,
the next hostname is tried. **But if one of them returns a result, the
lookup stops and the result is returned to the application.**

When only an *AAAA* record is returned, it appears to the caller that
*api.twilio.com* has no IPv4 address.

The end result: ``curl: (7) Couldn't connect to server``

Right now, I'm not aware of the exact versions of *CoreDNS* and its
plugins that make this so troublesome, but they are real. Workarounds to
these issues include:

* Adding IPv6 connectivity to your host.

* Forcing your client application to do IPv4 (passing the ``-4`` option
  to *curl*).

* Using hostnames that end in a period (*api.twilio.com.* will skip the
  domain suffix search).

However, none of those options are particularly appealing.


How nss-dns4only helps
----------------------

Instead of you having to patch all applications to specify ``AF_INET``
(for *curl* the ``-4`` option), *nss-dns4only* alleviates the problem by
translating all dual *A+AAAA* lookups to a *A* lookup.

It does so through the Name Server Switch (NSS) system, by hooking into
dual lookup call and forwarding the lookup to the single lookup call inside
the *glibc* ``libnss_dns`` module.


How to install
--------------

Install ``nss-dns4only.so`` as ``/lib/x86_64-linux-gnu/libnss_dns4only.so.2``
and run ``ldconfig``. ``make install`` will do this for you.

You alter the *hosts* line in ``/etc/nsswitch.conf``, inserting
*dns4only* before *dns*::

    hosts: files dns

Change it to::

    hosts: files dns4only [!UNAVAIL=return] dns

And now, this example call will return IPv4 addresses only:

.. code-block:: console

    $ python -c 'from socket import *; print(getaddrinfo("google.com",443))'

(To test functionality, you can replace ``dns4only`` with
``dns4suffix``. Then only lookups for *DOMAIN.v4* will get the
``dns4only`` treatment.)

.. code-block:: console

    $ python -c 'from socket import *; print(getaddrinfo("google.com.v4",443))'


Debian packaging and download
-----------------------------

TODO: Here we want some download links for pre-built binaries and debian
packages.


Technical background/history
----------------------------

In 2008, in *glibc-2.9*, ``_nss_dns_gethostbyname4_r`` was introduced, when
*glibc* started doing ``A`` + ``AAAA`` lookups for ``getaddrinfo()`` for
the unspecified (``AF_UNSPEC``) family::

    commit 1eb946b93509b94db2bddce741f2f3b483418a6d
    Author: Ulrich Drepper <drepper@redhat.com>
    Date:   Sat May 10 23:27:39 2008 +0000

    (adds _nss_dns_gethostbyname4_r: nss-dns4only is useful since glibc-2.9+)

    commit d1fe1f22192f27425accde26c562f456d835e74a
    Author: Ulrich Drepper <drepper@redhat.com>
    Date:   Wed Sep 15 10:10:05 2004 +0000
    (adds _nss_dns_gethostbyname3_r: nss-dns4only breaks before glibc-2.3.4+)

*glibc* transforms ``getaddrinfo()`` calls to calls to one or more of
the Name Server Switch (NSS) functions. *nss-dns4only* inserts a
``_nss_dns4only_gethostbyname4_r`` handler.

When ``_nss_dns4only_gethostbyname4_r`` is called, it calls into *glibc*
``libnss_dns`` directly, selecting only the ``AF_INET`` *address family.*

The *glibc manual* has information about
`Adding-another-Service-to-NSS`_ (version 2), about
`Actions-in-the-NSS-configuration`_ (``[!UNAVAIL=return]``). More
detailed info is in ``./resolv/nss_dns/dns-host.c`` and
``./sysdeps/posix/getaddrinfo.c`` (see ``__nss_lookup_function``).

*Sidenote: if your system uses libnss-resolve on localhost, you may
already get IPv4 only responses. Note that that only works in
conjunction with systemd-resolved.*


/Walter Doekes, OSSO B.V. 2020

.. _`Actions-in-the-NSS-configuration`: https://www.gnu.org/software/libc/manual/html_node/Actions-in-the-NSS-configuration.html#Actions-in-the-NSS-configuration
.. _`Adding-another-Service-to-NSS`: https://www.gnu.org/software/libc/manual/html_node/Adding-another-Service-to-NSS.html#Adding-another-Service-to-NSS
.. _`CoreDNS autopath+cache cause NXDOMAIN for A or AAAA records`: https://github.com/coredns/coredns/issues/3765
.. _`CoreDNS fails to resolve A records under circumstances`: https://github.com/coredns/coredns/issues/2842
.. _`Name Resolution Issue In CoreDNS`: https://www.linkedin.com/pulse/name-resolution-issue-coredns-inside-mind-problem-solver-spitzer/
