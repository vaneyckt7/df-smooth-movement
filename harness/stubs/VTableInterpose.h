#pragma once
namespace DFHack {
template<class Base,class Ptr> struct VMethodInterposeLink {
	Ptr chain; template<class F> VMethodInterposeLink(Ptr orig,F,int,const char*):chain(orig){}
	bool apply(){return true;} void remove(){} };
}
#define DEFINE_VMETHOD_INTERPOSE(rtype,name,args) \
	typedef rtype (interpose_base::*interpose_ptr_##name)args; \
	static DFHack::VMethodInterposeLink<interpose_base,interpose_ptr_##name> interpose_##name; \
	rtype interpose_fn_##name args
#define IMPLEMENT_VMETHOD_INTERPOSE(cls,name) \
	DFHack::VMethodInterposeLink<cls::interpose_base,cls::interpose_ptr_##name> cls::interpose_##name(&cls::interpose_base::name,&cls::interpose_fn_##name,0,#cls"::"#name);
#define INTERPOSE_NEXT(name) (this->*interpose_##name.chain)
#define INTERPOSE_HOOK(cls,name) (cls::interpose_##name)
