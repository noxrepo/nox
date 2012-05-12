include ../Make.vars

bin_PROGRAMS = 

bin_SCRIPTS = \
	reset-admin-pw \
	nox-monitor \
	switch_command.py

dist_noinst_SCRIPTS = 			\
	dot-vm \
	import.py.in \
	monitor \
	nox-monitor \
	start-test-vm \
	stop-test-vm \
	reset-admin-pw \
	switch_command.py \
	text2cc

all-local:

import.py: import.py.in Makefile
	sed -e 's&[@]pkglocalstatedir[@]&$(pkglocalstatedir)&' \
		< $(srcdir)/import.py.in > import.py.tmp
	printf '# Local variables:\n# buffer-read-only: t# End:' \
		>> import.py.tmp
	chmod a+x import.py.tmp
	mv import.py.tmp import.py
