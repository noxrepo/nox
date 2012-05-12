#! /bin/sh -e

if [ $# -ne 1 ]; then
     echo 1>&2 Usage: $0 confdir
     exit 127
fi

if [ ! -e $1/noxca.cnf ]; then
     echo 1>&2 Directory $1/noxca.cnf doesnt exist
     exit 127
fi

openssl genrsa -passout pass:INSECURE -des3 -out noxca.key 2048 >& 2 
chmod 0700 noxca.key
openssl rsa    -passin  pass:INSECURE -in noxca.key -out noxca.key.insecure >& 2 
chmod 0700 noxca.key.insecure
openssl req    -config $1/noxca.cnf -new -x509 -days 1001 -key noxca.key.insecure -out noxca.cert >& 2 
