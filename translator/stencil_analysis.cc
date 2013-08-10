// Copyright 2011, Tokyo Institute of Technology.
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#include "translator/stencil_analysis.h"

#include <boost/foreach.hpp>

#include "translator/translation_context.h"
#include "translator/physis_names.h"

using namespace std;
namespace si = SageInterface;

namespace physis {
namespace translator {
#if 0
    FOREACH(gsi, gs->begin(), gs->end()) {
      Grid *g = *gsi;
      g->access(offset);
      StencilRange &sr = uf.gr[g];
      LOG_DEBUG() << "Current range: " << sr.toString() << "\n";
      sr.insert(offset);
      LOG_DEBUG() << "After insertion:: " << sr.toString() << "\n";
    }
#endif

static bool getIntValue(SgExpression *exp, ssize_t &ret) {
  if (isSgIntVal(exp)) {
    ret = isSgIntVal(exp)->get_value();
    return true;
  } else if (isSgLongIntVal(exp)) {
    ret = isSgLongIntVal(exp)->get_value();    
    return true;
  } else if (isSgLongLongIntVal(exp)) {
    ret = isSgLongLongIntVal(exp)->get_value();    
    return true;
  } else if (isSgUnsignedIntVal(exp)) {
    ret = isSgUnsignedIntVal(exp)->get_value();    
    return true;
  } else {
    return false;
  }
}

static bool AnalyzeVarRef(SgVarRefExp *vref,
                          SgFunctionDeclaration *kernel,
                          int &dim) {
  SgInitializedNamePtrList &args = kernel->get_args();
  SgInitializedName *vn = vref->get_symbol()->get_declaration();
  ENUMERATE (i, ait, args.begin(), args.end()) {
    if (*ait == vn) {
      // NOTE: dimenstion starts at 1 (dimension value of 0 is
      // reserved for the cases where no index var is used)
      dim = i+1;
      LOG_DEBUG() << "Reference to index " << dim << " found\n";
      return true;
    }
  }
  return false;
}

// Legitimate expression:
// - (integer constant) * (index variable) + integer constant
bool AnalyzeStencilIndex(SgExpression *arg, StencilIndex &idx,
                         SgFunctionDeclaration *kernel) {
  ssize_t v;
  if (getIntValue(arg, v)) {
    idx.offset = v;
    idx.dim = 0;
    LOG_DEBUG() << "Integer constant: " << v << "\n";
    return true;
  }

  // var ref
  if (isSgVarRefExp(arg)) {
    int dim;
    if (AnalyzeVarRef(isSgVarRefExp(arg), kernel, dim)) {
      idx.dim = dim;
      idx.offset = 0;
      LOG_DEBUG() << "Index variable reference\n";
      return true;
    } else {
      LOG_DEBUG() << "Invalid variable reference\n";
    }
  }
    
  if (isSgAddOp(arg) || isSgSubtractOp(arg)) {
    SgBinaryOp *bop = isSgBinaryOp(arg);
    SgExpression *rhs = bop->get_rhs_operand();
    SgExpression *lhs = bop->get_lhs_operand();
    StencilIndex rhs_index;
    StencilIndex lhs_index;
    AnalyzeStencilIndex(rhs, rhs_index, kernel);
    AnalyzeStencilIndex(lhs, lhs_index, kernel);
    if (rhs_index.dim != 0 && lhs_index.dim != 0) {
      LOG_DEBUG() << "Invalid: two variable use\n";
      PSAbort(1);
    } else if (rhs_index.dim == 0 && lhs_index.dim == 0) {
      LOG_ERROR() << "Invalid: LHS and RHS are both constant, but this should not happen because constant folding is applied before this routine.\n";
      PSAbort(1);
    }

    // Make the variable reference lhs
    if (rhs_index.dim != 0) {
      std::swap(rhs_index, lhs_index);
    }
    
    PSAssert(rhs_index.dim == 0);
    idx.dim = lhs_index.dim;
    idx.offset = rhs_index.offset;
    if (isSgSubtractOp(arg)) {
      idx.offset *= -1;
    }
    // This should be always true if constant folding works beyond brace
    // boundaries, but apparently it's not the case.
    // PSAssert(lhs_index.offset == 0);
    if (lhs_index.offset != 0) {
      idx.offset += lhs_index.offset;
    }
    return true;
  }

  if (isSgCastExp(arg)) {
    SgCastExp *ca = isSgCastExp(arg);
    return AnalyzeStencilIndex(ca->get_operand(), idx, kernel);
  }

  if (isSgMinusOp(arg)) {
    if (AnalyzeStencilIndex(isSgMinusOp(arg)->get_operand(), idx,
                            kernel)) {
      idx.dim *= -1;
      idx.offset *= -1;
      return true;
    }
  }

  LOG_WARNING() << "Invalid stencil index: "
                << arg->unparseToString() << " ("
                << arg->class_name() << ")\n";
  return false;
}

static void PropagateStencilRangeToGrid(StencilMap &sm, TranslationContext &tx) {
  BOOST_FOREACH(SgNode *node,
                rose_util::QuerySubTreeAttribute<GridVarAttribute>(tx.project())) {
    SgInitializedName *gn = isSgInitializedName(node);
    PSAssert(gn);
    GridVarAttribute *gva = rose_util::GetASTAttribute<GridVarAttribute>(gn);
    const GridSet *gs = tx.findGrid(gn);
    BOOST_FOREACH (Grid *g, *gs) {
      // Note: g is NULL if InitializedName can be initialized
      // with external variables. That happens if a stencil kernel is
      // not designated as static.
      if (g == NULL) {
        LOG_INFO() << "Externally passed grid not set with stencil range info.\n";
        continue;
      } 
      g->SetStencilRange(gva->sr());
      LOG_DEBUG() << "Grid stencil range: "
                  << *g << ", " << gva->sr() << "\n";
    }
  }
}

static void ExtractArrayIndices(SgNode *n, IntVector &v) {
  SgPntrArrRefExp *p = isSgPntrArrRefExp(n);
  if (p == NULL) return;
  int i;
  bool is_int = rose_util::GetIntLikeVal(p->get_rhs_operand(), i);
  if (!is_int) return;
  LOG_DEBUG() << "Index: " << i << "\n";
  v.push_back(i);
  return ExtractArrayIndices(p->get_parent(), v);
}

static bool GetGridMember(SgFunctionCallExp *get_call,
                          string &member,
                          IntVector &indices) {
  SgDotExp *dot = isSgDotExp(get_call->get_parent());
  if (dot == NULL) return false;
  SgVarRefExp *rhs = isSgVarRefExp(dot->get_rhs_operand());
  PSAssert(rhs);
  member = rose_util::GetName(rhs);
  ExtractArrayIndices(dot->get_parent(), indices);
  std::reverse(indices.begin(), indices.end());
  return true;
}

static void AddIndex(SgFunctionCallExp *get_call,
                     SgInitializedName *gv,
                     StencilIndexList &sil, int nd) {

  string member;
  IntVector indices;
  // Collect member-specific information
  if (GetGridMember(get_call, member, indices)) {
    LOG_DEBUG() << "Access to member: " << member << "\n";
    GridVarAttribute *gva =
        rose_util::GetASTAttribute<GridVarAttribute>(gv);
    gva->AddMemberStencilIndexList(member, indices, sil);
  }    
  GridVarAttribute *gva =
      rose_util::GetASTAttribute<GridVarAttribute>(gv);
  gva->AddStencilIndexList(sil);
  return;
}

void AnalyzeStencilRange(StencilMap &sm, TranslationContext &tx) {
  SgFunctionDeclaration *kernel = sm.getKernel();
  SgFunctionCallExpPtrList get_calls
      = tx.getGridGetCalls(kernel->get_definition());
  SgFunctionCallExpPtrList get_periodic_calls
      = tx.getGridGetPeriodicCalls(kernel->get_definition());
  get_calls.insert(get_calls.end(), get_periodic_calls.begin(),
                   get_periodic_calls.end());
  FOREACH (it, get_calls.begin(), get_calls.end()) {
    SgFunctionCallExp *get_call = *it;
    LOG_DEBUG() << "Get call detected: "
                << get_call->unparseToString() << "\n";
    SgInitializedName *gv = GridType::getGridVarUsedInFuncCall(get_call);
    SgExpressionPtrList &args = get_call->get_args()->get_expressions();
    int nd = args.size();
    // Extracts a list of stencil indices
    StencilIndexList stencil_indices;
    ENUMERATE (dim, ait, args.begin(), args.end()) {
      LOG_DEBUG() << "get argument: " <<
          dim << ", " << (*ait)->unparseToString() << "\n";
      StencilIndex si;
      SgExpression *arg = *ait;
      // Simplify the analysis
      LOG_DEBUG() << "Analyzing " << arg->unparseToString() << "\n";
      si::constantFolding(arg->get_parent());
      LOG_VERBOSE() << "Constant folded: " << arg->unparseToString() << "\n";
      
      if (!AnalyzeStencilIndex(arg, si, kernel)) {
        PSAbort(1);
      }
      stencil_indices.push_back(si);
    }
    // Associate gv with the stencil indices
    AddIndex(get_call, gv, stencil_indices, nd);
    // Associate GridGet with the stencil indices    
    tx.registerStencilIndex(get_call, stencil_indices);
    LOG_DEBUG() << "Analyzed index: " << stencil_indices << "\n";
    // Attach GridGetAttribute to the get expression.
    // NOTE: registerStencilIndex will be replaced with the attribute
    bool is_periodic = tx.getGridFuncName(get_call) ==
        GridType::get_periodic_name;
    // A kernel function is analyzed multiple times if it appears
    // multiple times in use at stencil_map.
    if (rose_util::GetASTAttribute<GridGetAttribute>(
            get_call) == NULL) {
      GridType *gt = rose_util::GetASTAttribute<GridType>(gv);
      PSAssert(gt);
      GridGetAttribute *gga = new GridGetAttribute(
          gt, NULL, rose_util::GetASTAttribute<GridVarAttribute>(gv),
          tx.isKernel(kernel), is_periodic, &stencil_indices);
      rose_util::AddASTAttribute(get_call, gga);
    }
    if (is_periodic) sm.SetGridPeriodic(gv);
  }
  PropagateStencilRangeToGrid(sm, tx);
}

static SgInitializedName *FindInitializedName(const string &name,
                                              SgScopeStatement *scope) {
  SgVariableSymbol *vs = si::lookupVariableSymbolInParentScopes(
      name, scope);
  if (vs == NULL) return NULL;
  if (vs->get_scope() != scope) {
    LOG_DEBUG() << "InitializedName found, but not in the given scope.\n";
    return NULL;
  }
  return vs->get_declaration();
}

static void AnalyzeArrayOffsets(string s,
                                vector<string> &offsets) {
  LOG_DEBUG() << "Anayzing offset string: " << s << "\n";
  size_t i = 0;
  while (s[i] == '[') {
    size_t p = s.find_first_of(']', i);
    string x = s.substr(i+1, p-i-1);
    offsets.push_back(x);
    LOG_DEBUG() << "offset: " << x << "\n";
    i = p+1;
  }
  return;
}

static GridEmitAttribute *AnalyzeEmitCall(SgFunctionCallExp *fc) {
  SgExpression *emit_arg = fc->get_args()->get_expressions()[0];
  LOG_DEBUG() << "Emit arg: " << emit_arg->unparseToString() << "\n";
  string emit_str = isSgStringVal(emit_arg)->get_value();
  SgFunctionDefinition *kernel = si::getEnclosingFunctionDefinition(fc);
  PSAssert(kernel);
  // "g.v" -> gv: g, user_type: true, member_name: "v"
  // "g" -> gv: g, user_type: false
  size_t dot_pos = emit_str.find_first_of('.');
  string gv_str = (dot_pos == string::npos) ?
      emit_str : emit_str.substr(0, dot_pos);
  SgInitializedName *gv = FindInitializedName(gv_str, kernel);
  PSAssert(gv);
  GridType *gt = rose_util::GetASTAttribute<GridType>(gv);
  PSAssert(gt);
      
  GridEmitAttribute *attr = NULL;
  if (dot_pos == string::npos) {
    // No dot operator found
    attr = new GridEmitAttribute(gt, gv);
  } else {
    if (dot_pos != emit_str.find_last_of('.')) {
      LOG_ERROR() << "Multiple dot operators found: "
                  << emit_str << "\n";
      PSAbort(1);
    }
    LOG_DEBUG() << "User type emit\n";
    string member_name = emit_str.substr(dot_pos+1);
    size_t array_offset_pos = member_name.find_first_of('[');
    if (array_offset_pos != string::npos) {
      LOG_DEBUG() << "Element is an array\n";
      string offset_str = member_name.substr(array_offset_pos);
      member_name = member_name.substr(0, array_offset_pos);
      LOG_DEBUG() << "member name: " << member_name << "\n";
      vector<string> offsets;
      AnalyzeArrayOffsets(offset_str, offsets);
      attr = new GridEmitAttribute(gt, gv, member_name, offsets);
      LOG_DEBUG() << "Number of dimensions: " << offsets.size() << "\n";
    } else {
      LOG_DEBUG() << "member name: " << member_name << "\n";
      attr = new GridEmitAttribute(gt, gv, member_name);
    }
  }
  return attr;
}

void AnalyzeEmit(SgFunctionDeclaration *func) {
  LOG_DEBUG() << "AnalyzeEmit\n";
  SgNodePtrList calls =
      NodeQuery::querySubTree(func, V_SgFunctionCallExp);
  FOREACH (it, calls.begin(), calls.end()) {
    SgFunctionCallExp *fc = isSgFunctionCallExp(*it);
    PSAssert(fc);
    GridEmitAttribute *attr = NULL;
    if (GridType::isGridTypeSpecificCall(fc) &&
        GridType::GetGridFuncName(fc) == PS_GRID_EMIT_NAME) {
      SgInitializedName *gv = GridType::getGridVarUsedInFuncCall(fc);
      GridType *gt = rose_util::GetASTAttribute<GridType>(gv);      
      attr = new GridEmitAttribute(gt, gv);
    } else if (isSgFunctionRefExp(fc->get_function()) &&
               rose_util::getFuncName(fc) == PS_GRID_EMIT_UTYPE_NAME) {
      attr = AnalyzeEmitCall(fc);
    } else {
      continue;
    }
    LOG_DEBUG() << "Emit call detected: "
                << fc->unparseToString() << "\n";
    rose_util::AddASTAttribute(fc, attr);
  }
}

bool AnalyzeGetArrayMember(SgDotExp *get, SgExpressionVector &indices,
                           SgExpression *&parent) {
  bool ret = false;
  SgNode *p = get->get_parent();
  while (isSgPntrArrRefExp(p)) {
    ret = true;
    parent = isSgExpression(p);
    SgExpression *idx = isSgPntrArrRefExp(p)->get_rhs_operand();
    LOG_DEBUG() << "Array member index: "
                << idx->unparseToString() << "\n";
    indices.push_back(idx);
    p = p->get_parent();
  }
  
  return ret;
}

} // namespace translator
} // namespace physis
