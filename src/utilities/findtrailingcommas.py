#!/usr/bin/env python

import os, re

trail_newline_re = re.compile('(.*,\s*[\}\]])', re.M)
slash_comment = re.compile('//.*$', re.M)
comment = re.compile('/\*.*?\*/', re.S)
regexps = re.compile(r'(?<!\\)/.*?(?<!\\)/', re.M)
string1 = re.compile(r"(?<!\\)'.*?(?<!\\)'", re.M)
string2 = re.compile(r'(?<!\\)".*?(?<!\\)"', re.M)

def main():
    cwd = os.getcwd()
    cwd_len = len(cwd)

    for root, dirs, files in os.walk(cwd):
        if 'CVS' in dirs:
            dirs.remove('CVS')
        if '.svn' in dirs:
            dirs.remove('.svn')
        if '.git' in dirs:
            dirs.remove('.git')

        for file in files:
            if file[-3:] != '.js':
                continue

            fn = os.path.join(root, file)
            contents = open(fn).read()

            contents = slash_comment.sub('', contents)
            contents = comment.sub('', contents)
            contents = regexps.sub('//', contents)
            contents = string1.sub("''", contents)
            contents = string2.sub('""', contents)

            m = trail_newline_re.search(contents)

            if trail_newline_re.search(contents):
                for g in m.groups():
                    print '.%s: %r' % (fn[cwd_len:],g)

if __name__ == '__main__':
    main()
