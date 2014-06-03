#!/usr/bin/python
#
# Copyright 2013-2014 Ettus Research LLC
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

from xml.etree import ElementTree
from collections import namedtuple
import optparse
import base64
import hashlib
import os
import sys

# Parse options
parser = optparse.OptionParser()
parser.add_option("--uhd-images-path", type="string", dest="uhd_images_path", help="Install location for UHD images", default='')
parser.add_option("--merge-bin", type="string", dest="merge_bin", help="Path to bin file that needs to be merged with the LVBITX before exporting", default=None)
parser.add_option("--output-bin", action="store_true", dest="output_bin", help="Generate a binary FPGA programming bitstream file", default=False)
parser.add_option("--output-lvbitx-path", type="string", dest="output_lvbitx_path", help="Output path for autogenerated LVBITX file", default=None)
parser.add_option("--output-src-path", type="string", dest="output_src_path", help="Output path for autogenerated src file", default=None)
(options, args) = parser.parse_args()

# Args
if (len(args) < 1):
    print 'ERROR: Please specify the input LVBITX file name'
    sys.exit(1)

lvbitx_filename = args[0]
input_filename = os.path.abspath(lvbitx_filename)
autogen_src_path = os.path.abspath(options.output_src_path) if (options.output_src_path is not None) else os.path.dirname(input_filename)
class_name = os.path.splitext(os.path.basename(input_filename))[0]

if (not os.path.isfile(input_filename)):
    print 'ERROR: FPGA File ' + input_filename + ' could not be accessed or is not a file.'
    sys.exit(1)
if (options.merge_bin is not None and not os.path.isfile(os.path.abspath(options.merge_bin))):
    print 'ERROR: FPGA Bin File ' + options.merge_bin + ' could not be accessed or is not a file.'
    sys.exit(1)
if (not os.path.exists(autogen_src_path)):
    print 'ERROR: Output path ' + autogen_src_path + ' could not be accessed.'
    sys.exit(1)
if (options.output_lvbitx_path is not None and input_filename == os.path.join(autogen_src_path, class_name + '.lvbitx')):
    print 'ERROR: Input and output LVBITX files were the same. Choose a difference input file or output path.'
    sys.exit(1)

# Get XML Tree Node
tree = ElementTree.parse(input_filename)
root = tree.getroot()
codegen_transform = {}

# General info
codegen_transform['autogen_msg'] = '// Auto-generated file: DO NOT EDIT!\n// Generated from a LabVIEW FPGA LVBITX image using "process-lvbitx.py"'
codegen_transform['lvbitx_search_paths'] = options.uhd_images_path.replace('\\', '\\\\')
codegen_transform['lvbitx_classname'] = class_name
codegen_transform['lvbitx_classname_u'] = class_name.upper()
bitstream_version = root.find('BitstreamVersion').text

# Enumerate registers (controls and indicators)
register_list = root.find('VI').find('RegisterList')

reg_init_seq = ''
control_list = ''
indicator_list = ''
control_idx = 0
indicator_idx = 0
for register in register_list.findall('Register'):
    reg_type = 'INDICATOR' if (register.find('Indicator').text.lower() == 'true') else 'CONTROL'
    reg_name = '\"' + register.find('Name').text + '\"'

    if (reg_type == 'INDICATOR'):
        indicator_list += '\n    ' + reg_name + ','
        idx = indicator_idx
        indicator_idx += 1
    else:
        control_list += '\n    ' + reg_name + ','
        idx = control_idx
        control_idx += 1

    reg_init_seq += '\n    vtr.push_back(nirio_register_info_t('
    reg_init_seq += hex(int(register.find('Offset').text)) + ', '
    reg_init_seq += reg_type + 'S[' + str(idx) + '], '
    reg_init_seq += reg_type
    reg_init_seq += ')); //' + reg_name


codegen_transform['register_init'] = reg_init_seq
codegen_transform['control_list'] = control_list
codegen_transform['indicator_list'] = indicator_list

# Enumerate FIFOs
nifpga_metadata = root.find('Project').find('CompilationResultsTree').find('CompilationResults').find('NiFpga')
dma_channel_list = nifpga_metadata.find('DmaChannelAllocationList')
reg_block_list = nifpga_metadata.find('RegisterBlockList')

fifo_init_seq = ''
out_fifo_list = ''
in_fifo_list = ''
out_fifo_idx = 0
in_fifo_idx = 0
for dma_channel in dma_channel_list:
    fifo_name = '\"' + dma_channel.attrib['name'] + '\"'
    direction = 'OUTPUT_FIFO' if (dma_channel.find('Direction').text == 'HostToTarget') else 'INPUT_FIFO'
    for reg_block in reg_block_list.findall('RegisterBlock'):
        if (reg_block.attrib['name'] == dma_channel.find('BaseAddressTag').text):
            base_addr = reg_block.find('Offset').text
            break

    if (direction == 'OUTPUT_FIFO'):
        out_fifo_list += '\n    ' + fifo_name + ','
        idx = out_fifo_idx
        out_fifo_idx += 1
    else:
        in_fifo_list += '\n    ' + fifo_name + ','
        idx = in_fifo_idx
        in_fifo_idx += 1

    fifo_init_seq += '\n    vtr.push_back(nirio_fifo_info_t('
    fifo_init_seq += dma_channel.find('Number').text + ', '
    fifo_init_seq += direction + 'S[' + str(idx) + '], '
    fifo_init_seq += direction + ', '
    fifo_init_seq += str.lower(base_addr) + ', '
    fifo_init_seq += dma_channel.find('NumberOfElements').text + ', '
    fifo_init_seq += 'SCALAR_' + dma_channel.find('DataType').find('SubType').text + ', '
    fifo_init_seq += dma_channel.find('DataType').find('WordLength').text + ', '
    fifo_init_seq += bitstream_version
    fifo_init_seq += ')); //' + fifo_name


codegen_transform['fifo_init'] = fifo_init_seq
codegen_transform['out_fifo_list'] = out_fifo_list
codegen_transform['in_fifo_list'] = in_fifo_list

# Merge bitstream into LVBITX
if (options.merge_bin is not None):
    with open(os.path.abspath(options.merge_bin), 'rb') as bin_file:
        bitstream = bin_file.read()
        bitstream_md5 = hashlib.md5(bitstream).hexdigest()
        bitstream_b64 = base64.b64encode(bitstream)
        bitstream_b64_lb = ''
        for i in range(0, len(bitstream_b64), 76):
            bitstream_b64_lb += bitstream_b64[i:i+76] + '\n'

        root.find('Bitstream').text = bitstream_b64_lb
        root.find('BitstreamMD5').text = bitstream_md5

codegen_transform['lvbitx_signature'] = str.upper(root.find('SignatureRegister').text)

# Write BIN file
bitstream = base64.b64decode(root.find('Bitstream').text)
if (options.output_lvbitx_path is not None and hashlib.md5(bitstream).hexdigest() != root.find('BitstreamMD5').text):
    print 'ERROR: The MD5 sum for the output LVBITX was incorrect. Make sure that the bitstream in the input LVBITX or BIN file is valid.'
    sys.exit(1)
if (options.output_bin):
    fpga_bin_file = open(os.path.join(options.output_lvbitx_path, class_name + '.bin'), 'w')
    fpga_bin_file.write(bitstream)
    fpga_bin_file.close()

# Save LVBITX
if (options.output_lvbitx_path is not None):
    tree.write(os.path.join(options.output_lvbitx_path, class_name + '_fpga.lvbitx'), encoding="utf-8", xml_declaration=True, default_namespace=None, method="xml")

# Save HPP and CPP
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'template_lvbitx.hpp'), 'r') as template_file:
    template_string = template_file.read()
with open(os.path.join(autogen_src_path, class_name + '_lvbitx.hpp'), 'w') as source_file:
    source_file.write(template_string.format(**codegen_transform))

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'template_lvbitx.cpp'), 'r') as template_file:
    template_string = template_file.read()
with open(os.path.join(autogen_src_path, class_name + '_lvbitx.cpp'), 'w') as source_file:
    source_file.write(template_string.format(**codegen_transform))
