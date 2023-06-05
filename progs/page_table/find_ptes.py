
import os
import struct
import sys

# This script analyses the memory dumps produced by qemu_runner.py, which
# runs privesc.cc in a QEMU VM.  It estimates what proportion of bit flips
# in page table entries (PTEs) would be exploitable.


page_size = 0x1000


# Returns the index of the highest set bit, plus 1.
def HighestBit(val):
  assert val >= 0
  bit = 0
  while val != 0:
    bit += 1
    val >>= 1
  return bit


def Main():
  filename = 'out/memory_dump'
  fh = open(filename)
  pfn_count = os.stat(filename).st_size / page_size
  print 'Memory dump size: %i MB' % (os.stat(filename).st_size >> 20)
  print 'PFN count:', pfn_count

  def ReadBytes(offset, size):
    fh.seek(offset)
    return fh.read(size)

  def GetPtes(pfn):
    return struct.unpack('512Q', ReadBytes(pfn * page_size, page_size))

  # Locate the data pages via the markers they contain.  Look for the
  # markers written by privesc.cc.
  data_pages_by_pfn = {}
  data_pages_count = 0
  marker = struct.pack('Q', 0x43215678)
  for pfn in xrange(pfn_count):
    if ReadBytes(pfn * page_size, 8) == marker:
      val = struct.unpack('Q', ReadBytes(pfn * page_size + 8, 8))[0]
      data_pages_by_pfn[pfn] = val
      data_pages_count = max(data_pages_count, val + 1)
  print 'Found data pages:', data_pages_count
  assert len(data_pages_by_pfn) == data_pages_count

  # Locate the page tables via their characteristic contents.
  expected_pte = dict((idx, (1 << 63) | (pfn << 12))
                      for pfn, idx in data_pages_by_pfn.iteritems())
  mask = ((1 << 64) - 1) & ~0xfff
  target_pts = {}
  for pfn in xrange(pfn_count):
    vals = GetPtes(pfn)
    # Since we're using fast mode, only check the first entry.
    for i in xrange(data_pages_count / 512):
      if vals[0] & mask == expected_pte[i * 512]:
        target_pts[pfn] = i

  print '\nDiagram of page types in physical memory:'
  for pfn in xrange(pfn_count):
    ch = '.'
    if pfn in data_pages_by_pfn:
      ch = 'D'
    elif pfn in target_pts:
      ch = str(target_pts[pfn])
    sys.stdout.write(ch)
    if (pfn + 1) % 64 == 0:
      sys.stdout.write(' %i MB\n' % (pfn >> (20 - 12)))

  # Count how many bit flips cause PTEs to point to page tables.
  print '\nBit flip analysis:'
  total_hits_pts = 0
  total_hits_data = 0
  for bit in xrange(HighestBit(pfn_count)):
    hits_pts = 0
    hits_data = 0
    for data_pfn in data_pages_by_pfn.iterkeys():
      new_pfn = data_pfn ^ (1 << bit)
      if new_pfn in target_pts:
        hits_pts += 1
        total_hits_pts += 1
      elif new_pfn in data_pages_by_pfn:
        hits_data += 1
        total_hits_data += 1
    print 'bit %2i: %3i PT hits (%4.1f%%) (and %3i data page hits) out of %i' % (
        bit, hits_pts,
        float(hits_pts) / data_pages_count * 100,
        hits_data, data_pages_count)
  print 'total hits for page tables (exploitable): %i' % total_hits_pts
  print 'total hits for data pages (unexploitable): %i' % total_hits_data

  print 'page table pages: %i' % len(target_pts)


Main()
