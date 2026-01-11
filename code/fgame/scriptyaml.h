#ifndef __SCRIPTYAML_H__
#define __SCRIPTYAML_H__

#include "g_local.h"
#include "script/scriptvariable.h"
#include "corepp/listener.h"

class ScriptYAML : public Listener
{
private:
    void *m_yamlNode; // Void pointer to hide YAML::Node details from header

public:
    CLASS_PROTOTYPE(ScriptYAML);

    ScriptYAML();
    virtual ~ScriptYAML();

    void Load(Event *ev);
    void Get(Event *ev);
    void Dump(Event *ev);
};

#endif /* __SCRIPTYAML_H__ */
