import xml.etree.ElementTree as ET

tree = ET.parse("assets/deck_cards/victorian_steampunk/svg/spades_13_king.svg")
root = tree.getroot()
for i in range(min(10, len(root))):
    child = root[i]
    print(f"Child {i}: transform={child.attrib.get('transform')}, fill={child.attrib.get('fill')}")
