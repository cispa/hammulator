
import os
import subprocess
import sys


# We need to implement line splitting ourselves to avoid Python's buffering.
def ReadLines(stream):
  got = ''
  while True:
    ch = stream.read(1)
    got += ch
    if ch == '\n':
      yield got
      got = ''
    elif ch == '':
      yield got
      return


def Main():
  mem_size = 4000

  # Search for 'kvm_intel' or 'kvm_amd'.
  use_kvm = any(line.startswith('kvm_') for line in open('/proc/modules'))
  print 'use_kvm=%s' % use_kvm
  if use_kvm:
    cmd = 'kvm'
  else:
    cmd = 'qemu-system-x86_64'

  read_fd, write_fd = os.pipe()
  cmd += ' -kernel bzImage -initrd out/initrd.gz'
  cmd += ' -m %i -monitor stdio -display none' % mem_size
  cmd += ' -append console=ttyS0 -serial file:/dev/fd/%i' % write_fd
  log_fh = open('out/log', 'w')
  proc = subprocess.Popen(cmd, shell=True,
                          stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE)
  for line in ReadLines(os.fdopen(read_fd, 'r')):
    log_fh.write(line)
    sys.stdout.write(line)
    if 'SPRAY_END' in line:
      break
  print 'Sending command...'
  proc.stdin.write('pmemsave 0 %i out/memory_dump\n' % (mem_size << 20))
  proc.stdin.write('quit\n')
  print 'Wait...'
  proc.wait()


Main()
