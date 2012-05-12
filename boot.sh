#! /bin/sh -e

have_ext=$(if test -e src/ext/Makefile.am; then echo yes; else echo no; fi)
for opt
do
    case $opt in
        (--apps-core) core_only=yes ;;
        (--apps-net)  net_only=yes ;;
        (--enable-ext) have_ext=yes ;;
        (--disable-ext) have_ext=no ;;
        (--help) cat <<EOF
$0: bootstrap NOX from a Git repository
usage: $0 [OPTIONS]
The recognized options are:
  --enable-ext      include noxext
  --disable-ext     exclude noxext
  --apps-core       only build with core apps
  --apps-net        build with core and net apps
By default, noxext is included if it is present.
EOF
        exit 0
        ;;
        (*) echo "unknown option $opt; use --help for help"; exit 1 ;;
    esac
done

if test -e src/ext/boot.sh ; then
    (cd src/ext && ./boot.sh)
fi

if test "$core_only" = yes; then
    echo 'building with only core apps'
    sed -e "s/APPS_ID/core/" -e "s/TURN_ON_NETAPPS/no/" configure.ac.in > configure.ac
    if test -e ./installer; then
        echo "AC_CONFIG_FILES([installer/Makefile installer/build installer/decompress installer/package/installer " >> configure.ac
    else
        echo "AC_CONFIG_FILES([ " >> configure.ac
    fi
    find . -path "*Makefile.am" | grep -vE "\<apache-log4cxx\/|\<ext\>|\<protobuf\/|\<netapps\>" | sed -e "s/\.\<am\>//" -e "s/\.\///" >> configure.ac
    echo "])  " >> configure.ac
elif test "$net_only" = yes; then
    echo 'building with core and net apps'
    sed -e "s/APPS_ID/core/" -e "s/TURN_ON_NETAPPS/yes/" configure.ac.in > configure.ac
    if test -e ./installer; then
        echo "AC_CONFIG_FILES([installer/Makefile installer/build installer/decompress installer/package/installer " >> configure.ac
    else
        echo "AC_CONFIG_FILES([ " >> configure.ac
    fi

    find . -path "*Makefile.am" | grep -vE "\<apache-log4cxx\/|\<ext\>|\<protobuf\/>" | sed -e "s/\.\<am\>//" -e "s/\.\///" >> configure.ac
    echo "])  " >> configure.ac
else    
    echo 'building with all apps'
    sed -e "s/APPS_ID/full/" -e "s/TURN_ON_NETAPPS/yes/" configure.ac.in > configure.ac
    if test -e ./installer; then
        echo "AC_CONFIG_FILES([installer/Makefile installer/build installer/decompress installer/package/installer " >> configure.ac
    else
        echo "AC_CONFIG_FILES([ " >> configure.ac
    fi
    find . -path "*Makefile.am" | grep -vE "\<apache-log4cxx\/|\<ext\>|\<protobuf\/" | sed -e "s/\.\<am\>//" -e "s/\.\///" >> configure.ac
    echo "])  " >> configure.ac
fi    

# Enable or disable ext.
if test "$have_ext" = yes; then
    echo 'Enabling noxext...'

    extsubdirs=`find src/ext -name "configure.ac" | grep -v "\<protobuf\/"  | sed -e "s/configure.ac//" -e's%/$%%'`

    cat <<EOF >> configure.ac
#
# Automatically included by boot.sh
#
if test "\$noext" = false; then
    AC_CONFIG_SUBDIRS([$extsubdirs])
fi

EOF
else
    echo 'Disabling noxext...'
fi

echo "AC_OUTPUT  " >> configure.ac

# Bootstrap configure system from .ac/.am files
autoreconf --install -I `pwd`/m4 --force

