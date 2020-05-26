# -*- mode: rpm-spec-mode; encoding: utf-8; -*-
# Pass '--without docs' to rpmbuild if you don't want the documentation to be build

Summary: 	pspg: a unix pager optimized for psql
Name: 		pspg
Version: 	3.1.2
Release: 	0%{?dist}
License: 	BSD
Group: 		Development/Tools
Vendor: 	Pavel Stehule <pavel.stehule@gmail.com>
URL: 		https://github.com/okbob/pspg
Source: 	https://github.com/okbob/pspg/archive/%{version}.tar.gz
BuildRequires: 	ncurses-devel readline-devel libpq-devel
BuildRoot: 	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: 	ncurses readline libpq

%description
psps is a unix pager optimized for psql. It can freeze rows, freeze
columns, and lot of color themes are included.

%prep
%setup -q -n pspg

%build
%configure
CFLAGS="$RPM_OPT_FLAGS"
%{__make} %{_smp_mflags} \
	prefix=%{_prefix} \
	all

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
CFLAGS="$RPM_OPT_FLAGS"
%{__make} %{_smp_mflags} DESTDIR=$RPM_BUILD_ROOT \
	prefix=%{_prefix} bindir=%{_bindir} mandir=%{_mandir} \
	install

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{_bindir}/*

%changelog
* Tue May 19 2020 Pavel Stehule <pavel.stehule@gmail.com>
- code cleaning
- option skip-columns-like
- column names dynamic positioning

* Mon Apr  6 2020 Pavel Stehule <pavel.stehule@gmail.com>
- streaming mode for file (requires inotify)
- named pipe can be source stream

* Fri Mar 27 2020 Pavel Stehule <pavel.stehule@gmail.com>
- integration inotify check of input file

* Thu Dec 12 2019 Pavel Stehule <pavel.stehule@gmail.com>
- possibility to specify NULL string

* Sun Nov 24 2019 Pavel Stehule <pavel.stehule@gmail.com>
- tsv format suppport

* Fri Nov 15 2019 Pavel Stehule <pavel.stehule@gmail.com>
- fix entering string on CentOS 7.7
- try to process -F without ncurses start
- infrastructure cleaning

* Mon Nov  4 2019 Pavel Stehule <pavel.stehule@gmail.com>
- materialize dependency on libpq
- add internal performance diagnostics
- few micro optimizations

* Sun Oct 27 2019 Pavel Stehule <pavel.stehule@gmail.com>
- non interactive mode for csv
- possibility to take data from query
- watch mode

* Wed Oct 9  2019 Pavel Stehule <pavel.stehule@gmail.com>
- better handling Escape and sigint signal
- more comfortable usage of readline input

* Wed Sep 25 2019 Pavel Stehule <pavel.stehule@gmail.com>
- pspg can be used as csv viewer

* Sun Sep 8 2019 Pavel Stehule <pavel.stehule@gmail.com>
- complete support (with multilines) of sort over columns with numeric values

* Thu Sep 5 2019 Pavel Stehule <pavel.stehule@gmail.com>
- initial possibility to sort by numeric column

* Sun Sep 1 2019 Pavel Stehule <pavel.stehule@gmail.com>
- column search

* Sat Aug 24 2019 Pavel Stehule <pavel.stehule@gmail.com>
- vertical (column) cursor support

* Wed Jul 24 2019 Pavel Stehule <pavel.stehule@gmail.com>
- fix minor issues - left scrolling and theme changing

* Mon Apr 8 2019 Pavel Stehule <pavel.stehule@gmail.com>
- fix minor issue related to draw menu, when terminal is resized

* Thu Mar 21 2019 Pavel Stehule <pavel.stehule@gmail.com>
- use higher 8 colours when it is possible (fix Fodora 30 issue)
- new themes
- new options: bold labels, bold cursors

* Mon Sep 10 2018 Pavel Stehule <pavel.stehule@gmail.com>
- possibility to show line numbers and hide cursor, menu and status bar
- new themes
- fix some bugs

* Thu Jul 19 2018 Pavel Stehule <pavel.stehule@gmail.com>
- menu support
- new themes

* Thu Apr 26 2018 Pavel Stehule <pavel.stehule@gmail.com>
- compile with readline when it is available - history support
- fix some bugs

* Fri Mar 16 2018 Pavel Stehule <pavel.stehule@gmail.com>
- lot of bugfixes related to searching
- code cleaning
- 8bit encoding support

* Sun Feb 11 2018 Pavel Stehule <pavel.stehule@gmail.com>
- fix few crash related when searching was used

* Fri Jan 12 2017 Pavel Stehule <pavel.stehule@gmail.com>
- possibility to replace ascii art by unicode

* Thu Dec 28 2017 Pavel Stehule <pavel.stehule@gmail.com>
- bookmarks
- searching is much better now

* Fri Dec 15 2017 Pavel Stehule <pavel.stehule@gmail.com>
- case insensitive searching

* Fri Dec 1 2017 Pavel Stehule <pavel.stehule@gmail.com>
- less like status bar

* Sat Nov 25 2017 Pavel Stehule <pavel.stehule@gmail.com>
- lot of fixes and new features

* Wed Sep 13 2017 Pavel Stehule <pavel.stehule@gmail.com>
- initial version

