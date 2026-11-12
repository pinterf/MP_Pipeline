#include "SelectThunkEvery.def.h"

class SelectThunkEvery : public SelectThunkEvery_parameter_storage_t, public GenericVideoFilter
{
public:
    SelectThunkEvery(PClip child, const SelectThunkEvery_parameter_storage_t& o, IScriptEnvironment* env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    virtual ~SelectThunkEvery();
};