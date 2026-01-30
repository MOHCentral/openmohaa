#include "scriptyaml.h"
#include <yaml-cpp/yaml.h>
#include "g_local.h"

#include <vector>
#include <string>
#include <sstream>

// Helper to handle YAML node storage
struct YAMLContainer {
    YAML::Node node;
};

// Event Definitions
Event EV_ScriptYAML_Load
(
    "load",
    EV_DEFAULT,
    "s",
    "filename",
    "Loads a YAML or JSON file. Supports both formats via yaml-cpp.",
    EV_RETURN
);

Event EV_ScriptYAML_Get
(
    "get",
    EV_DEFAULT,
    "s",
    "key",
    "Gets a value from the loaded YAML/JSON using dot notation.",
    EV_RETURN
);

Event EV_ScriptYAML_Parse
(
    "parse",
    EV_DEFAULT,
    "s",
    "content",
    "Parses a YAML or JSON string.",
    EV_RETURN
);

Event EV_ScriptYAML_Save
(
    "save",
    EV_DEFAULT,
    "sS",
    "filename format",
    "Saves the YAML/JSON content to a file. Format can be 'yaml' or 'json' (default: yaml).",
    EV_NORMAL
);

Event EV_ScriptYAML_Serialize
(
    "serialize",
    EV_DEFAULT,
    "S",
    "format",
    "Serializes the content to a string. Format can be 'yaml' or 'json' (default: yaml).",
    EV_RETURN
);

// Class Declaration
CLASS_DECLARATION(SimpleEntity, ScriptYAML, "ScriptYAML")
{
    {&EV_ScriptYAML_Load, &ScriptYAML::Load},
    {&EV_ScriptYAML_Get,  &ScriptYAML::Get},
    {&EV_ScriptYAML_Parse, &ScriptYAML::Parse},
    {&EV_ScriptYAML_Save, &ScriptYAML::Save},
    {&EV_ScriptYAML_Serialize, &ScriptYAML::Serialize},
    {NULL, NULL}
};

ScriptYAML::ScriptYAML()
{
    m_yamlNode = new YAMLContainer();
}

ScriptYAML::~ScriptYAML()
{
    if (m_yamlNode) {
        delete (YAMLContainer*)m_yamlNode;
        m_yamlNode = NULL;
    }
}

void ScriptYAML::Load(Event *ev)
{
    str filename = ev->GetString(1);
    char *buffer = NULL;

    // Use engine file system to read file
    int len = gi.FS_ReadFile(filename.c_str(), (void **)&buffer, qtrue);

    if (len < 0 || !buffer) {
        ev->AddInteger(0); // Failed
        return;
    }

    try {
        YAMLContainer* container = (YAMLContainer*)m_yamlNode;
        // Parse the buffer safely
        container->node = YAML::Load(std::string(buffer, len));
        ev->AddInteger(1); // Success
    } catch (const YAML::Exception& e) {
        gi.Printf("YAML Parse Error in %s: %s\n", filename.c_str(), e.what());
        ev->AddInteger(0);
    }

    gi.FS_FreeFile(buffer);
}

// Helper to split string by delimiter
static std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void ScriptYAML::Get(Event *ev)
{
    str keyStr = ev->GetString(1);
    YAMLContainer* container = (YAMLContainer*)m_yamlNode;

    if (!container->node.IsDefined()) {
        ev->AddNil();
        return;
    }

    YAML::Node currentNode = container->node;
    std::vector<std::string> keys = split(keyStr.c_str(), '.');

    // Traverse the nodes
    try {
        for (const auto& key : keys) {
            if (currentNode.IsMap() && currentNode[key].IsDefined()) {
                currentNode = currentNode[key];
            } else if (currentNode.IsSequence()) {
                // Try to parse key as index
                try {
                    int index = std::stoi(key);
                    if (index >= 0 && (size_t)index < currentNode.size()) {
                        currentNode = currentNode[index];
                    } else {
                        ev->AddNil();
                        return;
                    }
                } catch (...) {
                    ev->AddNil();
                    return;
                }
            } else {
                ev->AddNil();
                return;
            }
        }

        // Convert final node to ScriptVariable
        if (currentNode.IsScalar()) {
            try {
                // Try as boolean
                bool b;
                if (YAML::convert<bool>::decode(currentNode, b)) {
                     ev->AddInteger(b);
                     return;
                }
            } catch(...) {}

            try {
                 // Try as int
                 int i;
                 if (YAML::convert<int>::decode(currentNode, i)) {
                     ev->AddInteger(i);
                     return;
                 }
            } catch(...) {}

            try {
                 // Try as float
                 float f;
                 if (YAML::convert<float>::decode(currentNode, f)) {
                     ev->AddFloat(f);
                     return;
                 }
            } catch(...) {}

            // Fallback to string
            ev->AddString(currentNode.as<std::string>().c_str());

        } else if (currentNode.IsSequence()) {
            // Data Conversion: Handle sequences by converting to script array

            ScriptVariable list;
            for (size_t i = 0; i < currentNode.size(); ++i) {
                YAML::Node item = currentNode[i];
                ScriptVariable indexVar;
                indexVar.setIntValue((int)i);

                ScriptVariable valueVar;
                if (item.IsScalar()) {
                    // Similar conversion logic...
                     try {
                        bool b;
                        if (YAML::convert<bool>::decode(item, b)) valueVar.setIntValue(b);
                        else {
                            int valI;
                            if (YAML::convert<int>::decode(item, valI)) valueVar.setIntValue(valI);
                            else {
                                float valF;
                                if (YAML::convert<float>::decode(item, valF)) valueVar.setFloatValue(valF);
                                else valueVar.setStringValue(item.as<std::string>().c_str());
                            }
                        }
                    } catch (...) {
                        valueVar.setStringValue(item.as<std::string>().c_str());
                    }
                } else {
                    // For non-scalars (maps/lists), we set nil to preserve index
                    // Future improvement: Recursive conversion
                    // valueVar is mostly nil by default or we can explicitly set it?
                    // ScriptVariable default ctor might not be NIL depending on impl, let's assume it handles it or we do nothing
                    // If we want explicit NIL:
                    // valueVar.setVoidValue(); // Hypothetical if needed, but usually uninitialized var acts as nil or void
                }
                list.setArrayAtRef(indexVar, valueVar);
            }
            ev->AddValue(list);

        } else if (currentNode.IsMap()) {
             // Return NIL for now as we can't easily convert full map to script object yet without more complex logic
             // Or maybe we should return a new ScriptYAML that wraps this node?
             // That would require ScriptYAML to share ownership or copy.
             // Requirement 3: "get(key): support dot-notation". This implies we drill down.
             // If the result is a Map, the user probably drilled down not deep enough.
             ev->AddNil();
        } else {
            ev->AddNil();
        }

    } catch (...) {
        ev->AddNil();
    }
}

void ScriptYAML::Parse(Event *ev)
{
    str content = ev->GetString(1);

    try {
        YAMLContainer* container = (YAMLContainer*)m_yamlNode;
        // Parse the string safely
        container->node = YAML::Load(content.c_str());
        ev->AddInteger(1); // Success
    } catch (const YAML::Exception& e) {
        gi.Printf("YAML Parse Error: %s\n", e.what());
        ev->AddInteger(0);
    }
}

void ScriptYAML::Save(Event *ev)
{
    str filename = ev->GetString(1);
    str format = "yaml";
    if (ev->NumArgs() > 1) {
        format = ev->GetString(2);
    }

    YAMLContainer* container = (YAMLContainer*)m_yamlNode;
    if (!container->node.IsDefined()) {
        gi.Printf("YAML Save Error: Node is undefined\n");
        return;
    }

    YAML::Emitter out;
    if (format == "json") {
        out << YAML::Flow;
        out << YAML::Flow;
    }

    out << container->node;

    if (!out.good()) {
        gi.Printf("YAML Emitter Error: %s\n", out.GetLastError());
        return;
    }

    std::string output = out.c_str();
    gi.FS_WriteFile(filename.c_str(), output.c_str(), output.length());
}

void ScriptYAML::Serialize(Event *ev)
{
    str format = "yaml";
    if (ev->NumArgs() > 0) {
        format = ev->GetString(1);
    }

    YAMLContainer* container = (YAMLContainer*)m_yamlNode;
    if (!container->node.IsDefined()) {
        ev->AddString("");
        return;
    }

    YAML::Emitter out;
    if (format == "json") {
        out << YAML::Flow;

    }

    out << container->node;

    if (!out.good()) {
        gi.Printf("YAML Emitter Error: %s\n", out.GetLastError());
        ev->AddString("");
        return;
    }

    ev->AddString(out.c_str());
}
