#include "translator/mpi_openmp_translator.h"

#include "translator/translation_context.h"
#include "translator/translation_util.h"
#include "translator/mpi_runtime_builder.h"

namespace pu = physis::util;
namespace si = SageInterface;
namespace sb = SageBuilder;

namespace physis {
namespace translator {


static std::string getUnitLoopIndexName(void) { 
  return "unit";
}

static std::string getDivisionArrayName(void) {
  return "div_omp";
}

static std::string getDivisionIndexName(void) {
  return "i_idx";
}

static std::string getDivisionWidthName(void) {
  return "i_width";
}

static std::string getLoopIndexName(unsigned int d) {
  return "count_i" + toString(d);
}

static std::string getCoordIdxName(unsigned int d) {
  return "x_" + toString(d);
}

static std::string getLoopIndexStartName(void) {
  return "i_start";
}

static std::string getLoopIndexEndName(void) {
  return "i_end";
}

static std::string getDivDenName() {
  return "i_den";
}

static std::string getDimensionName() {
  return "i_dim";
}

static std::string getWidthDimensionName() {
  return "i_width_dim";
}

static std::string getCacheArrayName(void) {
  return "cache_size";
}

static std::string getCacheLoopIdxName(unsigned int d) {
  return "count_c" + toString(d);
}

SgBasicBlock* MPIOpenMPTranslator::BuildRunKernelBody(
    StencilMap *s, SgInitializedName *stencil_param
                                                      ) {
  LOG_DEBUG() << "Generating run kernel body\n";
  SgBasicBlock *block = sb::buildBasicBlock();
  si::attachComment(block, "Generated by BuildRunKernelBody");

  SgExpression *stencil = sb::buildVarRefExp(stencil_param);
  SgExpression *minField = BuildStencilDomMinRef(stencil);
  SgExpression *maxField = BuildStencilDomMaxRef(stencil);  
  SgBasicBlock *loopBlock = block;

  LOG_DEBUG() << "Generating compute unit loop\n";

  int maxdim = s->getNumDim();
  PSAssert(3 >= maxdim);

  {

    SgVariableDeclaration *unitindexDecl =
        sb::buildVariableDeclaration(
            getUnitLoopIndexName(),
            sb::buildUnsignedLongType(),
            sb::buildAssignInitializer(
                sb::buildUnsignedLongVal(0)
                                       ),
            loopBlock);
    si::appendStatement(unitindexDecl, loopBlock);

    // divDecl begin
    SgVariableDeclaration *divDecl = 
        sb::buildVariableDeclaration(
            getDivisionArrayName(),
            sb::buildArrayType(sb::buildUnsignedLongType()),
            NULL, loopBlock
                                     );
    {
      SgExprListExp *divDefList = sb::buildExprListExp();
      int i;
      for (i = 0; i < maxdim; i++) {
        si::appendExpression(
            divDefList,
            sb::buildUnsignedLongVal(division_[i])
                             );
      }
      SgAggregateInitializer *divInit =
          sb::buildAggregateInitializer(
              divDefList,
              sb::buildArrayType(sb::buildUnsignedLongType())
                                        );
      divDecl->reset_initializer(divInit);
    }
    si::appendStatement(divDecl, loopBlock);
    // divDecl end

    // cacheDecl begin
    SgVariableDeclaration *cacheDecl = 
        sb::buildVariableDeclaration(
            getCacheArrayName(),
            sb::buildArrayType(sb::buildUnsignedLongType()),
            NULL
                                     );
    {
      SgExprListExp *cacheDefList = sb::buildExprListExp();
      int i;
      for (i = 0; i < maxdim; i++) {
        si::appendExpression(
            cacheDefList,
            sb::buildUnsignedLongVal(cache_size_[i])
                             );
      }
      SgAggregateInitializer *cacheInit =
          sb::buildAggregateInitializer(
              cacheDefList,
              sb::buildArrayType(sb::buildUnsignedLongType())
                                        );
      cacheDecl->reset_initializer(cacheInit);
    }
    si::appendStatement(cacheDecl, loopBlock);
    // cacheDecl end

    {
      SgBasicBlock *FixDivOmpBlock = sb::buildBasicBlock();
      SgVariableDeclaration *tmpiDecl =
          sb::buildVariableDeclaration(
              "i_tmp",
              sb::buildUnsignedLongType(),
              NULL,
              FixDivOmpBlock
                                       );
      si::appendStatement(tmpiDecl, FixDivOmpBlock);

      SgStatement *init =
          sb::buildAssignStatement(
              sb::buildVarRefExp(tmpiDecl),
              sb::buildIntVal(0)
                                   );
      SgStatement *test =
          sb::buildExprStatement(
              sb::buildLessThanOp(
                  sb::buildVarRefExp(tmpiDecl),
                  sb::buildIntVal(maxdim)
                                  ));
      SgExpression *incr = sb::buildPlusPlusOp(
          sb::buildVarRefExp(tmpiDecl));

      SgBasicBlock *FixDiv_innerBlock = sb::buildBasicBlock();
      SgStatement *loopStatement = sb::buildForStatement(
          init, test, incr, FixDiv_innerBlock);
      si::appendStatement(loopStatement, FixDivOmpBlock);

      SgExpression *divExpression= sb::buildPntrArrRefExp(
          sb::buildVarRefExp(divDecl),
          sb::buildVarRefExp(tmpiDecl)
                                                          );
      SgVariableDeclaration* WidthdimDecl =
          sb::buildVariableDeclaration(
              getWidthDimensionName(),
              sb::buildUnsignedLongType(),
              sb::buildAssignInitializer(
                  sb::buildSubtractOp(
                      sb::buildPntrArrRefExp(
                          si::copyExpression(maxField), sb::buildVarRefExp(tmpiDecl)),
                      sb::buildPntrArrRefExp(
                          si::copyExpression(minField), sb::buildVarRefExp(tmpiDecl))
                                      )
                                         ),
              FixDiv_innerBlock);
      si::appendStatement(WidthdimDecl, FixDiv_innerBlock);
      SgStatement *assignFixDev =
          sb::buildAssignStatement(
              si::copyExpression(divExpression),
              sb::buildConditionalExp(
                  sb::buildLessThanOp(
                      sb::buildVarRefExp(WidthdimDecl),
                      si::copyExpression(divExpression)
                                      ),
                  sb::buildVarRefExp(WidthdimDecl),
                  si::copyExpression(divExpression)
                                      )
                                   );
      si::appendStatement(assignFixDev, FixDiv_innerBlock);
      {
        SgStatement *condstmt =
            sb::buildExprStatement(
                sb::buildNotOp(
                    sb::buildVarRefExp(WidthdimDecl)
                               ));
        SgIfStmt *returnnowstmt =
            sb::buildIfStmt(
                condstmt,
                sb::buildReturnStmt(),
                NULL);
        si::appendStatement(returnnowstmt, FixDiv_innerBlock);
      }

      SgVariableDeclaration* WidthDivDecl =
          sb::buildVariableDeclaration(
              "i_width_division",
              sb::buildUnsignedLongType(),
              sb::buildAssignInitializer(
                  sb::buildDivideOp(
                      sb::buildVarRefExp(WidthdimDecl),
                      si::copyExpression(divExpression)
                                    )
                                         ),
              FixDiv_innerBlock);
      si::appendStatement(WidthDivDecl, FixDiv_innerBlock);
      {
        SgExpression *CacheExpression= sb::buildPntrArrRefExp(
            sb::buildVarRefExp(cacheDecl),
            sb::buildVarRefExp(tmpiDecl)
                                                              );
        SgStatement *condstmt =
            sb::buildExprStatement(
                sb::buildLessThanOp(
                    sb::buildVarRefExp(WidthDivDecl),
                    si::copyExpression(CacheExpression)
                                    )
                                   );
        SgStatement *truestmt =
            sb::buildAssignStatement(
                si::copyExpression(CacheExpression),
                sb::buildVarRefExp(WidthDivDecl)
                                     );
        SgIfStmt *resetCachestmt =
            sb::buildIfStmt(
                condstmt, truestmt, NULL);
        si::appendStatement(resetCachestmt, FixDiv_innerBlock);
      }

      si::appendStatement(FixDivOmpBlock, loopBlock);
    }
    
    SgBasicBlock *innerBlock = sb::buildBasicBlock();
    {
      SgStatement *loopStatement;
      SgStatement *init =
          sb::buildAssignStatement(
              sb::buildVarRefExp(unitindexDecl),
              sb::buildUnsignedLongVal(0));
      SgStatement *test;
      SgExpression *testmaxExp = sb::buildPntrArrRefExp(
          sb::buildVarRefExp(divDecl), sb::buildIntVal(0));
      {
        int i;
        for (i = 1; i < maxdim; i++) {
          testmaxExp = sb::buildMultiplyOp(
              testmaxExp,
              sb::buildPntrArrRefExp(
                  sb::buildVarRefExp(divDecl),
                  sb::buildIntVal(i)
                                     )
                                           );
        }
        test = sb::buildExprStatement(
            sb::buildLessThanOp(
                sb::buildVarRefExp(unitindexDecl),
                testmaxExp
                                )
                                      );
      }
      SgExpression *incr = sb::buildPlusPlusOp(sb::buildVarRefExp(unitindexDecl));
      loopStatement = sb::buildForStatement(init, test, incr, innerBlock);
    
#if 0
      {
        SgExprListExp *args = sb::buildExprListExp();
        args->append_expression(sb::buildIntVal(unit));
      
        SgFunctionCallExp *call =
            sb::buildFunctionCallExp("omp_set_num_threads", sb::buildVoidType(), args);
        si::appendStatement(sb::buildExprStatement(call), loopBlock);
      }
    
      {
        SgFunctionSymbol *printf
            = si::lookupFunctionSymbolInParentScopes("printf", global_scope_);
      
        SgExprListExp *args = sb::buildExprListExp();
        args->append_expression(sb::buildStringVal("omp_get_num_procs: %d\\n" "omp_get_max_threads: %d\\n"));
        args->append_expression(sb::buildFunctionCallExp("omp_get_num_procs", sb::buildSignedIntType()));
        args->append_expression(sb::buildFunctionCallExp("omp_get_max_threads", sb::buildSignedIntType()));
      
        SgFunctionCallExp *call =
            sb::buildFunctionCallExp(printf, args);
        si::appendStatement(sb::buildExprStatement(call), loopBlock);
      }
#endif

      si::appendStatement(sb::buildPragmaDeclaration("omp parallel for schedule(static,1)"), loopBlock);
      si::appendStatement(loopStatement, loopBlock);
    }
    
    loopBlock = innerBlock;
    si::attachComment(loopBlock, "Division by OpenMP parallelization");
    si::attachComment(loopBlock, "Division by OpenMP parallelization end",
                      PreprocessingInfo::after);

#if 0
    {
      SgFunctionSymbol *printf
          = si::lookupFunctionSymbolInParentScopes("printf", global_scope_);
      
      SgExprListExp *args = sb::buildExprListExp();
      args->append_expression(sb::buildStringVal("omp_in_parallel: %d\\n" "omp_get_thread_num: %d\\n"));
      args->append_expression(sb::buildFunctionCallExp("omp_in_parallel", sb::buildSignedIntType()));
      args->append_expression(sb::buildFunctionCallExp("omp_get_thread_num", sb::buildSignedIntType()));
      
      SgFunctionCallExp *call =
          sb::buildFunctionCallExp(printf, args);
      si::appendStatement(sb::buildExprStatement(call), loopBlock);
    }
#endif

    {
      SgFunctionSymbol *symMPLoopInit = 
          si::lookupFunctionSymbolInParentScopes(
              "__PSInitLoop_OpenMP", global_scope_
                                                 );
      SgExprListExp *args =
          sb::buildExprListExp(
#if 0
              sb::buildVarRefExp(unitindexDecl)
#endif
                               );
      SgFunctionCallExp *expCallLoopInit =
          sb::buildFunctionCallExp(
              sb::buildFunctionRefExp(symMPLoopInit),
              args
                                   );
      si::appendStatement(
          sb::buildExprStatement(expCallLoopInit),
          loopBlock
                          );
    }

    // indStart begin
    SgVariableDeclaration *indStartDecl = 
        sb::buildVariableDeclaration(
            getLoopIndexStartName(),
            sb::buildArrayType(sb::buildUnsignedLongType()),
            NULL, loopBlock
                                     );
    {
      SgExprListExp *indStartDefList = sb::buildExprListExp();
      int i;
      for (i = 0; i < maxdim; i++) {
        si::appendExpression(
            indStartDefList,
            sb::buildUnsignedLongVal(0)
                             );
      }
      SgAggregateInitializer *indStartInit =
          sb::buildAggregateInitializer(
              indStartDefList,
              sb::buildArrayType(sb::buildUnsignedLongType())
                                        );
      indStartDecl->reset_initializer(indStartInit);
    }
    si::appendStatement(indStartDecl, loopBlock);
    // indStart end
    
    // indEnd begin
    SgVariableDeclaration *indEndDecl = 
        sb::buildVariableDeclaration(
            getLoopIndexEndName(),
            sb::buildArrayType(sb::buildUnsignedLongType()),
            NULL, loopBlock
                                     );
    {
      SgExprListExp *indEndDefList = sb::buildExprListExp();
      int i;
      for (i = 0; i < maxdim; i++) {
        si::appendExpression(
            indEndDefList,
            sb::buildUnsignedLongVal(0)
                             );
      }
      SgAggregateInitializer *indEndInit =
          sb::buildAggregateInitializer(
              indEndDefList,
              sb::buildArrayType(sb::buildUnsignedLongType())
                                        );
      indEndDecl->reset_initializer(indEndInit);
    }
    si::appendStatement(indEndDecl, loopBlock);
    // indEnd end
    
    // indWidth begin
    SgVariableDeclaration *indWidthDecl = 
        sb::buildVariableDeclaration(
            getDivisionWidthName(),
            sb::buildArrayType(sb::buildUnsignedLongType()),
            NULL, loopBlock
                                     );
    {
      SgExprListExp *indWidthDefList = sb::buildExprListExp();
      int i;
      for (i = 0; i < maxdim; i++) {
        si::appendExpression(
            indWidthDefList,
            sb::buildUnsignedLongVal(0)
                             );
      }
      SgAggregateInitializer *indWidthInit =
          sb::buildAggregateInitializer(
              indWidthDefList,
              sb::buildArrayType(sb::buildUnsignedLongType())
                                        );
      indWidthDecl->reset_initializer(indWidthInit);
    }
    si::appendStatement(indWidthDecl, loopBlock);
    // indWidth end
    
    SgVariableDeclaration *DivDenDecl =
        sb::buildVariableDeclaration(
            getDivDenName(), sb::buildUnsignedLongType(),
            sb::buildAssignInitializer(sb::buildUnsignedLongVal(1)),
            loopBlock);
    si::appendStatement(DivDenDecl, loopBlock);

    SgBasicBlock *preBlock = sb::buildBasicBlock();
    {
      SgVariableDeclaration *DimidxDecl =
          sb::buildVariableDeclaration(
              getDimensionName(), 
              sb::buildUnsignedLongType(), NULL, loopBlock);
      si::appendStatement(DimidxDecl, loopBlock);

      {
        SgStatement *loopStatement;
        SgStatement *init =
            sb::buildAssignStatement(
                sb::buildVarRefExp(DimidxDecl),
                sb::buildIntVal(0));
        SgStatement *test = sb::buildExprStatement(
            sb::buildLessThanOp(
                sb::buildVarRefExp(DimidxDecl),
                sb::buildUnsignedLongVal(maxdim)
                                )
                                                   );
        SgExpression *incr = sb::buildPlusPlusOp(sb::buildVarRefExp(DimidxDecl));
        loopStatement = sb::buildForStatement(init, test, incr, preBlock);
        si::appendStatement(loopStatement, loopBlock);
      }

      SgExpression *divExpression= sb::buildPntrArrRefExp(
          sb::buildVarRefExp(divDecl),
          sb::buildVarRefExp(DimidxDecl)
                                                          );
      SgExpression *indStartExpression = sb::buildPntrArrRefExp(
          sb::buildVarRefExp(indStartDecl),
          sb::buildVarRefExp(DimidxDecl)
                                                                );
      SgExpression *indEndExpression = sb::buildPntrArrRefExp(
          sb::buildVarRefExp(indEndDecl),
          sb::buildVarRefExp(DimidxDecl)
                                                              );
      SgExpression *indWidthExpression = sb::buildPntrArrRefExp(
          sb::buildVarRefExp(indWidthDecl),
          sb::buildVarRefExp(DimidxDecl)
                                                                );

      loopBlock = preBlock;

      SgVariableDeclaration* divisionIndexDecl =
          sb::buildVariableDeclaration(
              getDivisionIndexName(),
              sb::buildUnsignedLongType(),
              NULL, loopBlock);
      si::appendStatement(divisionIndexDecl, loopBlock);
     
      SgStatement *assignDivisionIndex =
          sb::buildAssignStatement(
              sb::buildVarRefExp(divisionIndexDecl),
              sb::buildModOp(
                  sb::buildDivideOp(
                      sb::buildVarRefExp(unitindexDecl),
                      sb::buildVarRefExp(DivDenDecl)),
                  si::copyExpression(divExpression)
                             ));
      
      SgStatement *assignDenMulDiv =
          sb::buildExprStatement(
              sb::buildMultAssignOp(
                  sb::buildVarRefExp(DivDenDecl),
                  si::copyExpression(divExpression)
                                    )
                                 );
      si::appendStatement(assignDivisionIndex, loopBlock);
      si::appendStatement(assignDenMulDiv, loopBlock);
      
      SgVariableDeclaration* WidthdimDecl =
          sb::buildVariableDeclaration(
              getWidthDimensionName(),
              sb::buildUnsignedLongType(),
              sb::buildAssignInitializer(
                  sb::buildSubtractOp(
                      sb::buildPntrArrRefExp(
                          si::copyExpression(maxField), sb::buildVarRefExp(DimidxDecl)),
                      sb::buildPntrArrRefExp(
                          si::copyExpression(minField), sb::buildVarRefExp(DimidxDecl))
                                      )
                                         ),
              loopBlock);
      si::appendStatement(WidthdimDecl, loopBlock);

      SgStatement *assignStart =
          sb::buildAssignStatement(
              si::copyExpression(indStartExpression),
              sb::buildAddOp(
                  sb::buildPntrArrRefExp(minField, sb::buildVarRefExp(DimidxDecl)),
                  sb::buildDivideOp(
                      sb::buildMultiplyOp(
                          sb::buildVarRefExp(divisionIndexDecl),
                          sb::buildVarRefExp(WidthdimDecl)
                                          ),
                      si::copyExpression(divExpression)
                                    )
                             )
                                   );
      SgStatement *assignEnd =
          sb::buildAssignStatement(
              si::copyExpression(indEndExpression),
              sb::buildAddOp(
                  sb::buildPntrArrRefExp(minField, sb::buildVarRefExp(DimidxDecl)),
                  sb::buildDivideOp(
                      sb::buildMultiplyOp(
                          sb::buildAddOp(
                              sb::buildVarRefExp(divisionIndexDecl),
                              sb::buildIntVal(1)
                                         ),
                          sb::buildVarRefExp(WidthdimDecl)
                                          ),
                      si::copyExpression(divExpression)
                                    )
                             )
                                   );
      SgStatement *assignEnd2 =
          sb::buildAssignStatement(
              si::copyExpression(indEndExpression),
              sb::buildConditionalExp(
                  sb::buildLessThanOp(
                      sb::buildVarRefExp(divisionIndexDecl),
                      sb::buildSubtractOp(
                          si::copyExpression(divExpression),
                          sb::buildIntVal(1)
                                          )
                                      ),
                  si::copyExpression(indEndExpression),
                  sb::buildPntrArrRefExp(
                      si::copyExpression(maxField),
                      sb::buildVarRefExp(DimidxDecl)
                                         )
                                      )
                                   );
      
      SgStatement *assignWidth =
          sb::buildAssignStatement(
              si::copyExpression(indWidthExpression),
              sb::buildSubtractOp(
                  si::copyExpression(indEndExpression),
                  si::copyExpression(indStartExpression)
                                  ));

      si::appendStatement(assignStart, loopBlock);
      si::appendStatement(assignEnd, loopBlock);
      si::appendStatement(assignEnd2, loopBlock);
      si::appendStatement(assignWidth, loopBlock);
    }

    LOG_DEBUG() << "Generating nested loop divided by cache size\n";
    loopBlock = innerBlock;

    SgBasicBlock *CallKernelOuterBlock = sb::buildBasicBlock();
    si::appendStatement(CallKernelOuterBlock, loopBlock);
    loopBlock = CallKernelOuterBlock;

    SgVariableDeclaration *i_indexMaxDecl[3] = {0, 0, 0};
    SgVariableDeclaration *i_indexDecl[3] = {0, 0, 0};

    SgExpressionPtrList indexArgs;

    LOG_DEBUG() << "Generating nested loop INSIDE cache size.\n";

    for (int i = maxdim - 1; i >= 0; i--) {
      SgExpression *i_indWidthExp =
          sb::buildPntrArrRefExp(
              sb::buildVarRefExp(indWidthDecl), sb::buildIntVal(i));
      SgExpression *i_CachesizeExp =
          sb::buildPntrArrRefExp(
              sb::buildVarRefExp(cacheDecl), sb::buildIntVal(i));
      i_indexMaxDecl[i] =
          sb::buildVariableDeclaration(
              getLoopIndexName(i) + "_max",
              sb::buildUnsignedLongType(),
#if 0
              NULL,
#else
              sb::buildAssignInitializer(
                  sb::buildDivideOp(
                      sb::buildAddOp(
                          si::copyExpression(i_indWidthExp),
                          sb::buildSubtractOp(
                              si::copyExpression(i_CachesizeExp),
                              sb::buildIntVal(1)
                                              )
                                     ),
                      si::copyExpression(i_CachesizeExp)
                                    )
                                         ),
#endif
              loopBlock
                                       );
      si::appendStatement(i_indexMaxDecl[i], loopBlock);
      i_indexDecl[i] =
          sb::buildVariableDeclaration(
              getLoopIndexName(i),
              sb::buildUnsignedLongType(),
              NULL, loopBlock);
      //indexArgs.insert(indexArgs.begin(), sb::buildVarRefExp(i_indexDecl));
      si::appendStatement(i_indexDecl[i], loopBlock);
    
      SgStatement *init =
          sb::buildAssignStatement(
              sb::buildVarRefExp(i_indexDecl[i]),
              sb::buildUnsignedLongVal(0));
      SgStatement *test =
          sb::buildExprStatement(
              sb::buildLessThanOp(
                  sb::buildVarRefExp(i_indexDecl[i]),
                  sb::buildVarRefExp(i_indexMaxDecl[i])
                                  ));
      SgExpression *incr = 
          sb::buildPlusPlusOp(sb::buildVarRefExp(i_indexDecl[i]));

      SgBasicBlock *inner_innerBlock = sb::buildBasicBlock();
      SgStatement *loopStatement = 
          sb::buildForStatement(init, test, incr, inner_innerBlock);
      si::appendStatement(loopStatement, loopBlock);
    
      loopBlock = inner_innerBlock;
    } // for (int i = maxdim - 1; i >= 0; i--)

    si::attachComment(loopBlock, "loop limited within cache block");
    si::attachComment(loopBlock, "loop limited within cache block end",
                      PreprocessingInfo::after);


    LOG_DEBUG() << "Writing actual kernel call part.\n";
    SgVariableDeclaration *c_coordDecl[3] = {0, 0, 0};
    SgVariableDeclaration *c_indexDecl[3] = {0, 0, 0};
    for (int c = maxdim - 1; c >= 0; c--) {
      c_indexDecl[c] =
          sb::buildVariableDeclaration(
              getCacheLoopIdxName(c),
              sb::buildUnsignedLongType(),
              NULL,
              loopBlock);
      //indexArgs.insert(indexArgs.begin(), sb::buildVarRefExp(i_indexDecl));
      si::appendStatement(c_indexDecl[c], loopBlock);
      c_coordDecl[c] =
          sb::buildVariableDeclaration(
              getCoordIdxName(c),
              sb::buildUnsignedLongType(),
              NULL,
              loopBlock);
      indexArgs.insert(indexArgs.begin(), sb::buildVarRefExp(c_coordDecl[c]));
      si::appendStatement(c_coordDecl[c], loopBlock);
    
      SgStatement *init =
          sb::buildAssignStatement(
              sb::buildVarRefExp(c_indexDecl[c]),
              sb::buildUnsignedLongVal(0));
      SgStatement *test =
          sb::buildExprStatement(
              sb::buildLessThanOp(
                  sb::buildVarRefExp(c_indexDecl[c]),
                  sb::buildPntrArrRefExp(
                      sb::buildVarRefExp(cacheDecl),
                      sb::buildIntVal(c)
                                         )
                                  ));
      SgExpression *incr = 
          sb::buildPlusPlusOp(sb::buildVarRefExp(c_indexDecl[c]));

      SgBasicBlock *inner_innerBlock = sb::buildBasicBlock();
      SgStatement *loopStatement = 
          sb::buildForStatement(init, test, incr, inner_innerBlock);
      si::appendStatement(loopStatement, loopBlock);
    
      loopBlock = inner_innerBlock;
    } // for (int c = maxdim - 1; c >= 0; i--)

    {
      for (int j = 0; j < maxdim; j++) {
        SgVarRefExp *lhs = sb::buildVarRefExp(c_coordDecl[j]);
        SgExpression *rhs = 
            sb::buildPntrArrRefExp(
                sb::buildVarRefExp(indStartDecl),
                sb::buildIntVal(j)
                                   );
        rhs = sb::buildAddOp(
            rhs,
            sb::buildMultiplyOp(
                sb::buildPntrArrRefExp(
                    sb::buildVarRefExp(cacheDecl),
                    sb::buildIntVal(j)
                                       ),
                sb::buildVarRefExp(i_indexDecl[j])
                                )
                             );
        rhs = sb::buildAddOp(
            rhs,
            sb::buildVarRefExp(c_indexDecl[j])
                             );
        SgExprStatement *j_coordAssignStmt =
            sb::buildAssignStatement(lhs, rhs);
        si::appendStatement(j_coordAssignStmt, loopBlock);

        SgStatement *condstmt = 
            sb::buildExprStatement(
                sb::buildLessOrEqualOp(
                    sb::buildPntrArrRefExp(
                        sb::buildVarRefExp(indEndDecl),
                        sb::buildIntVal(j)),
                    sb::buildVarRefExp(c_coordDecl[j])));
        SgIfStmt *returnCoordstmt =
            sb::buildIfStmt(
                condstmt,
                sb::buildContinueStmt(),
                NULL);
        si::appendStatement(returnCoordstmt, loopBlock);
      } // for (int j = 0; j < maxdim; j++)
    }

    {
      SgFunctionCallExp *kernelCall =
          BuildKernelCall(s, indexArgs, stencil_param);
      si::appendStatement(sb::buildExprStatement(kernelCall), loopBlock);
    }
  }

  return block;

} // BuildRunKernelBody



} // namespace translator
} // namespace physis

