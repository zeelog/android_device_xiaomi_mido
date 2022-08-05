# Copyright (c) 2020, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of The Linux Foundation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import os
import xml.etree.ElementTree as ET


ATTRIBUTE_ORDER = ['name', 'id', 'value']

XML_HEAD = """<?xml version="1.0" encoding="ISO-8859-1"?>"""
COPY_RIGHT = "<!--- copy right -->"


def getCopyRight():
    global COPY_RIGHT
    script_dir = os.path.dirname(os.path.abspath(__file__))
    copyright_path = os.path.join(script_dir, "copyright.txt")
    # print(copyright_path)
    f = open(copyright_path, 'r')
    COPY_RIGHT = f.read()
    f.close()


def write_xml_root_to_file_v2(file_path, super_root):
    for sub_node in super_root:
        mixer = sub_node
    data = gen_xml_string(mixer)
    full_xml = XML_HEAD+'\n'+COPY_RIGHT+'\n'+data
    with open(file_path, 'w') as f:
        f.write(full_xml)


def xml_to_map(xml_node, root_key=None, map=None, node_level=0):
    """Given xml node, generate map(dict)"""
    if not map:
        map = dict()
    if not root_key:
        current_key = get_key_for_node_only(xml_node, node_level)
    else:
        current_key = root_key + '->' + get_key_for_node_only(
            xml_node, node_level)
    map[current_key] = xml_node
    for sub_node in xml_node:
        xml_to_map(sub_node, current_key, map, node_level + 1)
    return map


def get_mixer_map(super_root):
    for node in super_root:
        mixer = node
    mixer_map = dict()
    for node in mixer:
        node_key = gen_xml_string(node)
        mixer_map[node_key] = node
    return mixer_map


def get_key_for_node_only(xml_node, level):
    """ Given xml node ,generate unique based on (node, node atrributes, node depth) only"""
    attr_str = ''
    for attrib_name in sorted(xml_node.keys()):
        attr_str += attrib_name.strip() + "=" + xml_node.get(
            attrib_name).strip() + ":"
    if attr_str != '':
        attr_str = attr_str[:-1]
    node_str = 'level=' + str(level) + ':' + xml_node.tag
    key_str = node_str + ":" + attr_str
    if xml_node.text != None and not xml_node.text.isspace():
        key_str = key_str + ":" + xml_node.text.strip()
    return key_str


def get_copy_xml_node(xml_node):
    """ Return Exact copy of xml node, return type as Element """
    new_attrib = dict()
    for at_name, at_value in xml_node.attrib.items():
        new_attrib[at_name.strip()] = at_value.strip()
    copy_node = ET.Element(xml_node.tag.strip(), new_attrib)
    if xml_node.text != None and not xml_node.text.isspace:
        copy_node.text = xml_node.text
    return copy_node


def copy_full_node(xml_node):
    """ Return Exact copy whole xml node including sub nodes, return type as Element """
    new_node = get_copy_xml_node(xml_node)
    for sub_node in xml_node:
        new_node.append(copy_full_node(sub_node))
    return new_node


def open_xml_root(filename):
    """ open xml filename add SuperRoot Element at top, return SuperRoot Node (Element) """
    try:
        xml_tree = ET.parse(filename)
        xml_root = xml_tree.getroot()
        super_root = ET.Element("SuperRoot")
        super_root.append(xml_root)
        super_root = copy_full_node(super_root)
    except:
        print('unable to open: '+filename+' as xml')
        raise
    return super_root


def gen_xml_string(xml_node, level=0):
    """Generate xml string for a given xml node with good indentation"""
    s = '<' + xml_node.tag + ' '
    if xml_node.attrib:
        for at_name in ATTRIBUTE_ORDER:
            at_value = xml_node.attrib.get(at_name, "ZEBRAAAA")
            if at_value != "ZEBRAAAA":
                s += at_name + '=\"' + at_value + '\" '
    space_str = ''
    for i in range(level):
        space_str += '    '
    if len(xml_node) > 0 or xml_node.text != None:
        s = s.strip()+'>\n'
        for sub_node in xml_node:
            s += space_str + '    ' + gen_xml_string(sub_node,
                                                     level + 1) + '\n'
        if xml_node.text:
            s += space_str + '    ' + xml_node.text + '\n'
        s += space_str + '</' + xml_node.tag + '>'
    else:
        s = s.strip()+'/>'
    return s


def print_map(map):
    for key, value in map.items():
        print(key, value)


def arrange_ctl_path_tags(super_root_ug):
    super_root = super_root = ET.Element("SuperRoot")
    new_mixer = ET.Element("mixer")
    super_root.append(new_mixer)
    for child in super_root_ug:
        mixer = child
    for child in mixer:
        if child.tag == 'ctl':
            new_child = copy_full_node(child)
            new_mixer.append(new_child)
    for child in mixer:
        if child.tag == 'path':
            new_child = copy_full_node(child)
            new_mixer.append(new_child)
    return super_root


def mixer_extract_union(xml_nodes):
    map = dict()
    super_root = super_root = ET.Element("SuperRoot")
    new_mixer = ET.Element("mixer")
    super_root.append(new_mixer)
    for xml_node in xml_nodes:
        for child in xml_node:
            mixer = child
        for child in mixer:
            key = "tag="+child.tag+":"+"name="+child.attrib.get('name', str(None))+":"+"id="\
                + child.attrib.get('id', str(None))
            if not map.get(key, False):
                new_child = copy_full_node(child)
                new_mixer.append(new_child)
                map[key] = True
    super_root = arrange_ctl_path_tags(super_root)
    return super_root


def mixer_extract_base(xml_node1, xml_node2):
    """ Assumption of SuperRoot is given for both node"""
    map1 = xml_to_map(xml_node1, map=None)
    for sub_node in xml_node2:
        mixer = sub_node
    new_mixer = get_copy_xml_node(mixer)
    super_root = get_copy_xml_node(xml_node2)
    super_root.append(new_mixer)
    level = 0
    current_key = get_key_for_node_only(
        xml_node2, level) + '->' + get_key_for_node_only(mixer, level + 1)
    level += 1
    for xml_child in mixer:
        if _mixer_extract_base(xml_child, current_key, map1, level + 1):
            child_copy = copy_full_node(xml_child)
            new_mixer.append(child_copy)
    return super_root


def _mixer_extract_base(mixer_child, root_key, base_map, node_level):
    current_key = root_key + "->" + get_key_for_node_only(
        mixer_child, node_level)
    if base_map.get(current_key, 0) == 0:
        return False
    for child in mixer_child:
        return _mixer_extract_base(child, current_key, base_map,
                                   node_level + 1)
    return True


def mixer_extract_overlay(super_base, super_mixer):
    for child in super_mixer:
        mixer = child
    base_map = get_mixer_map(super_base)
    new_mixer = get_copy_xml_node(mixer)
    super_root = get_copy_xml_node(super_mixer)
    super_root.append(new_mixer)
    for xml_child in mixer:
        xml_child_key = gen_xml_string(xml_child)
        node_elem=base_map.get(xml_child_key, False)
        if not isinstance(node_elem,ET.Element):
            child_copy = copy_full_node(xml_child)
            new_mixer.append(child_copy)
    return super_root


def _mixer_extract_overlay(mixer_child, root_key, base_map, node_level):
    current_key = root_key + "->" + get_key_for_node_only(
        mixer_child, node_level)
    if base_map.get(current_key, 0) == 0:
        return False
    for child in mixer_child:
        if not _mixer_extract_overlay(child, current_key, base_map,
                                      node_level + 1):
            return False
    return True


def seperate_ctl_path(super_root):
    for child in super_root:
        mixer = child
    path_nodes = list()
    for child in mixer:
        if child.tag == 'path':
            path_nodes.append(child)
    for path_node in path_nodes:
        mixer.remove(path_node)
    for path_node in path_nodes:
        mixer.append(path_node)
    return super_root


def mixer_combine(super_base, super_overlay):
    super_base = copy_full_node(super_base)
    super_overlay = copy_full_node(super_overlay)

    super_base = seperate_ctl_path(super_base)
    super_overlay = seperate_ctl_path(super_overlay)

    for child in super_base:
        base = child
    for child in super_overlay:
        overlay = child
    base_ctl_len = 0
    for child in base:
        if child.tag == 'path':
            break
        base_ctl_len += 1

    overlay_ctl_len = 0
    for child in overlay:
        if child.tag == 'path':
            break
        overlay_ctl_len += 1

    for i in range(overlay_ctl_len):
        base.insert(base_ctl_len + i, overlay[i])

    for i in range(overlay_ctl_len, len(overlay)):
        base.append(overlay[i])
    super_base = override_tag(super_base)
    super_base = sort_tag_depend(super_base)
    return super_base


def similar_tag_exists(mixer, node, node_index):
    l = 0
    for child in mixer:
        l += 1
        if l > node_index and child.tag == node.tag:
            if child != node \
                and child.attrib.get('name', None) == node.attrib.get('name', None) \
                    and child.attrib.get('id', None) == node.attrib.get('id', None):
                return True
    return False


def override_tag(super_combined):
    for child in super_combined:
        mixer = child
    child_nodes = list()
    l = 0
    for child in mixer:
        l += 1
        if similar_tag_exists(mixer, child, l):
            child_nodes.append(child)
    for child in child_nodes:
        mixer.remove(child)
    return super_combined


def sort_tag_depend(super_root):
    for node in super_root:
        mixer = node
    dep_map = dict()
    sub_node_list = list()
    start_flag = True
    start = 0
    path_start = 0
    all_nodes_map = dict()
    for sub_node in mixer:
        start += 1
        sub_node_list.append(sub_node)
        if sub_node.tag == "ctl":
            continue
        if start_flag:
            path_start = start
            start_flag = False
        key = sub_node.tag+":"+"name=" + \
            sub_node.attrib.get("name", str(None)) + "id=" + \
            sub_node.attrib.get("id", str(None))
        dep_list = list()
        all_nodes_map[key] = sub_node
        for child in sub_node:
            if child.tag == "ctl":
                continue
            dep_list.append(child)
        if len(dep_list) != 0:
            dep_map[key] = dep_list
        else:
            dep_map[key] = True
    # print("path_start:"+str(path_start))
    ctl_list = sub_node_list[0:path_start]
    path_list = sub_node_list[path_start:]
    new_path_list = _sort_tag_depend(
        all_nodes_map, path_list, dep_map, new_path_list=None)
    full_list = ctl_list+new_path_list
    mixer.clear()
    for node in full_list:
        mixer.append(node)
    return super_root


def _sort_tag_depend(all_nodes_map, path_list, dep_map, new_path_list=None):
    if new_path_list is None:
        new_path_list = list()

    for sub_node in path_list:
        key = sub_node.tag+":"+"name=" + \
            sub_node.attrib.get("name", str(None))+"id=" + \
            sub_node.attrib.get("id", str(None))
        res = dep_map.get(key, False)
        req_node = all_nodes_map.get(key, 0)
        if req_node == 0:
            print(gen_xml_string(sub_node))
            print("Error in ordering the mixer tags")
        if res == True:
            new_path_list.append(req_node)
        elif res == False:
            continue
        else:
            _sort_tag_depend(all_nodes_map, res, dep_map, new_path_list)
            new_path_list.append(req_node)
        dep_map[key] = False
    return new_path_list


def is_xmls_good(files):
    try:
        for file_name in files:
            super_root = open_xml_root(file_name)
        return True
    except:
        print('invalid xml file: ' + file_name)
        return False


def is_xml_good(file_name):
    try:
        super_root = open_xml_root(file_name)
        print('able to parse:'+file_name+' as xml')
        return True
    except:
        print('unable to parse:'+file_name+' as xml')
        raise
#############


def base_gen(args):
    l = len(args.files)
    file1 = open_xml_root(args.files[0])
    for i in range(1, l):
        file2 = open_xml_root(args.files[i])
        base = mixer_extract_base(file1, file2)
        file1 = copy_full_node(base)
    if not args.out:
        out = 'base.xml'
    else:
        [out] = args.out
    write_xml_root_to_file_v2(os.path.join(args.out_dir, out), file1)


def overlay_gen(args):
    l = len(args.files)
    base = open_xml_root(args.base)
    for i in range(0, l):
        file1 = open_xml_root(args.files[i])
        overlay = mixer_extract_overlay(base, file1)
        overlay = copy_full_node(overlay)
        write_xml_root_to_file_v2(os.path.join(
            args.out_dir, args.out[i]), overlay)


def combine_gen(args):
    l = len(args.overlay)
    base = open_xml_root(args.base)
    for i in range(0, l):
        overlay = open_xml_root(args.overlay[i])
        combine = mixer_combine(base, overlay)
        combine = copy_full_node(combine)
        write_xml_root_to_file_v2(os.path.join(
            args.out_dir, args.out[i]), combine)


def union_gen(args):
    l = len(args.files)
    file1 = open_xml_root(args.files[0])
    ul = list()
    ul.append(file1)
    for i in range(1, l):
        file2 = open_xml_root(args.files[i])
        ul.append(file2)
    file1 = mixer_extract_union(ul)
    if not args.out:
        out = 'mixer_union.xml'
    else:
        [out] = args.out
    write_xml_root_to_file_v2(os.path.join(args.out_dir, out), file1)


def main(args):
    getCopyRight()
    if args.check:
        check(args)
    if args.generate == 'base':
        base_gen(args)
    if args.generate == 'union':
        union_gen(args)
    if args.generate == 'overlay':
        overlay_gen(args)
    if args.generate == 'combine':
        combine_gen(args)


def is_valid_file_list(file_path_list):
    for file_path in file_path_list:
        if not os.path.isfile(file_path):
            print(file_path+' doesn\'t exist')
            return False
    return True


def gen_abs_paths(rel_path_list):
    l = list()
    for rel_path in rel_path_list:
        l.append(os.path.abspath(rel_path))
    return l


def validate(args):
    if args.check:
        if args.file:
            return args
        if not os.path.isfile(args.file1):
            print(args.file1+' is not a file')
            return False
        if not os.path.isfile(args.file2):
            print(args.file1+' is not a file')
            return False
        return args

    if not args.out_dir:
        args.out_dir = os.path.dirname(os.path.realpath(__file__))
    if not os.path.isdir(args.out_dir):
        print('out_dir doesn\'t exist')
        return False
    args.out_dir = os.path.abspath(args.out_dir)

    if args.generate == 'base' or args.generate == 'union':
        if not args.files:
            print('missing --files argument')
            return False
        if not is_valid_file_list(args.files):
            return False
        args.files = gen_abs_paths(args.files)
        if not is_xmls_good(args.files):
            return False
        return args

    if args.generate == 'overlay':
        if not args.base:
            print('missing --base argument')
            return False
        if not is_valid_file_list([args.base]):
            return False
        args.base = os.path.abspath(args.base)
        if not args.files:
            print('missing --files argument')
            return False
        if not is_valid_file_list(args.files):
            return False
        args.files = gen_abs_paths(args.files)
        if not is_xmls_good([args.base]):
            return False
        if not is_xmls_good(args.files):
            return False

        if not args.out:
            args.out = list()
            for i in range(len(args.files)):
                s = 'overlay'+str(i)+'.xml'
                args.out.append(s)
        elif not len(args.out) == len(args.files):
            return False
        return args

    if args.generate == 'combine':
        if not args.base:
            print('invalid base')
            return False
        if not is_valid_file_list([args.base]):
            return False
        args.base = os.path.abspath(args.base)
        if not args.overlay:
            print('no overlays')
            return False
        if not is_valid_file_list(args.overlay):
            return False
        args.overlay = gen_abs_paths(args.overlay)
        if not is_xmls_good([args.base]):
            return False
        if not is_xmls_good(args.overlay):
            return False
        if not args.out:
            args.out = list()
            for i in range(len(args.overlay)):
                s = 'combine'+str(i)+'.xml'
                args.out.append(s)
        elif not len(args.out) == len(args.overlay):
            return False
        return args


def _check_mixer_equivalent(sub_node, map2, root_key, level):
    current_key = root_key + "->" + get_key_for_node_only(sub_node, level)
    res = map2.get(current_key, 0)
    if res == 0:
        return False
    for child in sub_node:
        if not _check_mixer_equivalent(child, map2, current_key, level + 1):
            return False
    return True


def check_mixer_equivalent(root1, root2):
    for m in root1:
        mixer_node1 = m
    for m in root2:
        mixer_node2 = m
    map2 = xml_to_map(mixer_node2)
    node_level = 0
    current_key = get_key_for_node_only(mixer_node1, node_level)
    flag = True
    for sub_node in mixer_node1:
        if not _check_mixer_equivalent(sub_node, map2, current_key, node_level+1):
            print(gen_xml_string(sub_node))
            flag = False
    return flag


def checker_v3(node1, node2):
    for node in node1:
        mixer1 = node
    for node in node2:
        mixer2 = node
    map2 = dict()
    for sub_node in mixer2:
        key = gen_xml_string(sub_node)
        map2[key] = True
    flag = True
    for sub_node in mixer1:
        key = gen_xml_string(sub_node)
        if not map2.get(key, False):
            print(key)
            flag = False
    return flag


def check(args):
    if args.file:
        if not os.path.isfile(args.file):
            print(args.file+" doesn't exist")
        is_xml_good(args.file)
        return
    f1 = open_xml_root(args.file1)
    f2 = open_xml_root(args.file2)
    if checker_v3(f1, f2):
        print('file1 <= file2')
    print("=======================================================")
    if checker_v3(f2, f1):
        print('file2 <= file1')
    return


if __name__ == '__main__':
    arg_parser = argparse.ArgumentParser(
        description="Script to generate base,overlay or to combining base and overlay")
    arg_parser.add_argument('--files', nargs='+',
                            default=None)
    arg_parser.add_argument('--base', action='store', type=str,
                            default=None)
    arg_parser.add_argument('--union', action='store', type=str,
                            default=None)
    arg_parser.add_argument('--overlay', nargs='+',
                            default=None)
    arg_parser.add_argument('--out_dir', action='store', type=str,
                            default=None)
    arg_parser.add_argument('--out', nargs='+',
                            default=None)
    arg_parser.add_argument("--generate", type=str, choices=['base', 'overlay', 'combine', 'union'],
                            help="choose one among options", default=None)
    arg_parser.add_argument('--check', action='store_true', default=False)
    arg_parser.add_argument('--file', action='store', type=str, default=None)
    arg_parser.add_argument('--file1', action='store', type=str)
    arg_parser.add_argument('--file2', action='store', type=str)
    arg_parser.add_argument(
        '--copyright', action='store', type=str, default=None)

    args = arg_parser.parse_args()
    args = validate(args)
    if not args:
        print('xml_opt.py invalid arguments')
    else:
        main(args)
