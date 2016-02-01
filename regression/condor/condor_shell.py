#!/usr/bin/python

import optparse
import os
import sys

user = os.getenv("USER")
notify_user = '%s@cs.wisc.edu' % user
uwmf_requirements = '(Memory > 2900) && (Arch == "X86_64") && (OpSys == "LINUX") && (FileSystemDomain == "cs.wisc.edu" || FileSystemDomain == ".cs.wisc.edu") && (ClusterGeneration>=5) && (Machine != "ale-01.cs.wisc.edu") && (Machine != "ale-02.cs.wisc.edu") && (Machine != "clover-01.cs.wisc.edu") && (Machine != "clover-02.cs.wisc.edu") && (IsComputeCluster == True) && (IsDedicated == True)'

templatestart = \
'\
rank = TARGET.Memory\n\
universe = vanilla\n\
notification = Error\n\
+IsCommittedJob = TRUE\n\
notify_user = %s\n\
getenv = True\n\
requirements = %s\n\
\n\
' % (notify_user, uwmf_requirements)

def runCommand(command, override = False):
    if options.debug and not override:
        print command
    else:
        os.system(command)

def getFilePrefix(directory):
    suffixfilename = os.path.join(directory,'suffix.txt')
    if not os.path.exists(suffixfilename):
        suffixfile = open(suffixfilename, 'w')
        suffixfile.write('0')
        suffixfile.close()
    suffixfile = open(suffixfilename, 'r')
    currentsuffix = int(suffixfile.readline())
    suffixfile.close()
    deletecmd = 'rm -f %s' % suffixfilename
    runCommand(deletecmd, True)
    writecmd = 'echo %d > %s' % (currentsuffix + 1, suffixfilename)
    runCommand(writecmd, True)
    return '%010d' % currentsuffix

parser = optparse.OptionParser()
parser.add_option("--command", "-c", action="store", help="The command to submit to condor")
parser.add_option("--debug", "-d", action="store_true", default=False, help="Just print commands")
parser.add_option("--output", "-o", action="store", help="Redirect program output to this directory")
(options, args) = parser.parse_args()

if len(args):
    print "ERROR: Script doesn't take any positional arguments"
    sys.exit(-1)

if not options.command:
    print "ERROR: Must specify the command to execute with '-c'"
    sys.exit(-1)

# Verify existence of subdirectory for the generated submit scripts
moduledir = os.path.dirname(os.path.realpath(__file__))
condoroutdir = os.path.join(moduledir, 'jobfiles')
if not os.path.exists(condoroutdir):
    mkdircmd = 'mkdir -p %s' % condoroutdir
    runCommand(mkdircmd)
currentdir = os.getcwd()

# Get unique prefix for files
fileprefix = getFilePrefix(condoroutdir)
programoutfile = os.path.join(currentdir, 'std.%s.out' % fileprefix)
if options.output:
    programoutfile = os.path.abspath(options.output)
    programoutdir = os.path.dirname(programoutfile)
    if not os.path.exists(programoutdir):
        mkdircmd = 'mkdir -p %s' % programoutdir
        runCommand(mkdircmd)

# Generate the bash script
scriptname = '%s.script' % fileprefix
scriptfilename = os.path.join(condoroutdir, scriptname)
print "SHELL: submit wrapper script: %s" % scriptfilename
scriptfile = open(scriptfilename, 'w')
scriptfile.write('#!/bin/bash\n')
scriptfile.write('%s\n' % options.command)
# scriptfile.write('rm -f %s\n' % scriptfilename)
scriptfile.close()
chmodcmd = 'chmod +x %s' % scriptfilename
runCommand(chmodcmd)

hostname = os.getenv('HOSTNAME')

# Generate the submit script
submitname = '%s.submit' % fileprefix
submitfilename = os.path.join(condoroutdir, submitname)
print "SHELL: submit file: %s" % submitfilename
submitfile = open(submitfilename, 'w')
submitfile.write(templatestart)
submitfile.write('initialdir = %s\n' % currentdir)
submitfile.write('executable = %s\n' % scriptfilename)
logfilename = os.path.join(condoroutdir, '%s.log' % fileprefix)
print "SHELL: log file: %s" % logfilename
submitfile.write('log = %s\n' % logfilename)
submitfile.write('output = %s\n' % programoutfile)
submitfile.write('error = %s\n' % programoutfile)
submitfile.write('queue\n')
submitfile.close()

# Submit the job
submitcmd = 'condor_submit %s' % submitfilename
runCommand(submitcmd)

# Delete the submit file
deletecmd = 'rm -f %s' % submitfilename
# runCommand(deletecmd)

