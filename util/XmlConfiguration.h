#ifndef __XML_CONFIGURATION__
#define __XML_CONFIGURATION__

#include <string>
#include "rapidxml_utils.hpp"

class XmlConfiguration
{

public:
    XmlConfiguration(std::string filename);

    bool GetBool(std::string nodeName, std::string attributeName = "");
    int GetInt(std::string nodeName, std::string attributeName = "");
    std::string GetString(std::string nodeName, std::string attributeName = "");
    std::vector<std::string> GetStrings(std::string parentNodeName,
        std::string childNodeName = "", std::string attributeName = "");

private:
    rapidxml::file<> mXmlFile;
    rapidxml::xml_document<> mDoc;

};


#endif
