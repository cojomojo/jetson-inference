/**
 *  XmlConfiguration.cpp
 */

#include "log.h"
#include "XmlConfiguration.h"

#define LOG_XML_READER "[rapidxml]  "

XmlConfiguration::XmlConfiguration(std::string filename)
    : mXmlFile(rapidxml::file<>(filename.c_str()))
{
    if (mXmlFile.size() == 0) {
        LOG_ERROR(LOG_XML_READER, "The file " << filename << " is empty" << std::endl);
    }

    mDoc.parse<0>(mXmlFile.data());
}

int XmlConfiguration::GetInt(std::string nodeName, std::string attributeName)
{
    auto str = GetString(nodeName, attributeName);
    return std::stoi(str);
}

std::string XmlConfiguration::GetString(std::string nodeName, std::string attributeName)
{
    rapidxml::xml_node<> *node = mDoc.first_node(nodeName.c_str());

    if (attributeName != "")
        return node->first_attribute(attributeName.c_str())->value();

    return node->value();
}

std::vector<std::string> XmlConfiguration::GetStrings(std::string parentNodeName,
    std::string childNodeName, std::string attributeName)
{
    std::vector<std::string> values;

    rapidxml::xml_node<> *node = mDoc.first_node(parentNodeName.c_str());

    if (node == NULL) {
        LOG_ERROR(LOG_XML_READER, "Node is empty.\n");
        return values;
    }

    if ((childNodeName != "")) {
        node = node->first_node(childNodeName.c_str());
        while (node != NULL) {
            if (attributeName != "")
                values.push_back(node->first_attribute(attributeName.c_str())->value());
            else
                values.push_back(node->value());
            node = node->next_sibling(childNodeName.c_str());
        }
    } else {
        while (node != NULL) {
            if (attributeName != "")
                values.push_back(node->first_attribute(attributeName.c_str())->value());
            else
                values.push_back(node->value());
            node = node->next_sibling(parentNodeName.c_str());
        }
    }

    return values;
}
