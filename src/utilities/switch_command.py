#!/usr/bin/python
#
# Send arbitrary command to a switch
#

import getopt,sys,os
import httplib
import simplejson
import urllib

# TODO: need to set the path for this
from nox.webapps.webserviceclient.simple import PersistentLogin, NOXWSClient


def usage():
    print """
    Usage:

        switch_command.py -d <directory name> -s <switch name> -c <command> 
                          [-u <admin username>] [-p <admin passwd>] 
                          [args]

    e.g. switch_command -d Built-in -s foo -c restart 

    Note: accepts mangled switch names
    """

if __name__ == '__main__':

    sys.path.append('/opt/nox/bin')

    try:
        opts, args = getopt.getopt(sys.argv[1:], "hd:s:c:u:p:")
    except getopt.GetoptError, err:
        # print help information and exit:
        print str(err) # will print something like "option -a not recognized"
        usage()
        sys.exit(2)
    directory = None
    switch    = None 
    command   = None
    adminu    = "admin"
    adminp    = "admin"
    for o, a in opts:
        if o == "-h":
            usage()
            sys.exit()
        elif o == '-d': 
            directory = a
        elif o == '-s': 
            switch = a
            try:
                if switch.find(';') != -1:
                    directory = switch.split(';')[0]
                    switch =  switch.split(';')[0]
            except Exception, e:
                print 'Format error in mangled name',switch
                sys.exit()
        elif o == '-c': 
            command = a
        elif o == '-u': 
            adminu = a
        elif o == '-p': 
            adminp = a
        else:
            assert False, "unhandled option"

    if not directory or not switch or not command:
        usage()
        sys.exit()

    print ' Logging into web service.. ',
    loginmgr = PersistentLogin("admin","admin")
    # currently only support localhost
    wsc = NOXWSClient("127.0.0.1", 443, True, loginmgr)
    print 'done'

    urlstr = '/ws.v1/switch/'+directory+'/'+switch+'/command'
    print ' Issuing:'
    print '\t',urlstr
    url = urllib.quote(urlstr)

    d = {}
    d['command'] = command 
    d['args']    = args 
    headers = {}
    headers["content-type"] = "application/json"
    response = wsc.put(url, headers, simplejson.dumps(d))
    body = response.getBody()

    if body == '0':
        print 'Command sent succesfully'
    else:    
        print 'Error: ',body
