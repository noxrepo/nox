include ../Make.vars

dist_bin_SCRIPTS = \
	gen-nox-cert.sh 
bin_SCRIPTS = \
	gen-nox-cert.sh 
data_DATA = \
	noxca.cnf

pkgsysconf_DATA = \
	noxca.key.insecure \
	noxca.cert \
	nox.meta.json

EXTRA_DIST = \
	noxca.cnf \
	nox.meta.json

noxca.key.insecure noxca.cert :
	@echo "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
	@echo "++  Creating Development SSL certs for coreui  "
	@echo ""
	$(srcdir)/gen-nox-cert.sh $(srcdir)/
	@echo ""
	@echo "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"

all-local: noxca.key.insecure noxca.cert
	@dlist="$(pkgsysconf_DATA)";for f in $$dlist; do \
	  if test ! -f $$f; then \
		ln -sf $(srcdir)/$$f $(builddir)/$$f;\
	  fi;\
	done; 

clean-local:
	@find  . -type l -exec rm -rf {} \; 
	@rm -f noxca.key.insecure noxca.cert noxca.key
