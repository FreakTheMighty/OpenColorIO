/*
Copyright (c) 2003-2010 Sony Pictures Imageworks Inc., et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <OpenColorIO/OpenColorIO.h>

#include "Logging.h"
#include "Op.h"

#include <sstream>

OCIO_NAMESPACE_ENTER
{
    namespace
    {
        const int MAX_OPTIMIZATION_PASSES = 8;
        
        int RemoveNoOps(OpRcPtrVec & opVec)
        {
            int count = 0;
            
            OpRcPtrVec::iterator iter = opVec.begin();
            while(iter != opVec.end())
            {
                if((*iter)->isNoOp())
                {
                    iter = opVec.erase(iter);
                    ++count;
                }
                else
                {
                    ++iter;
                }
            }
            
            return count;
        }
        
        int RemoveInverseOps(OpRcPtrVec & opVec)
        {
            int count = 0;
            int firstindex = 0; // this must be a signed int
            
            while(firstindex < static_cast<int>(opVec.size()-1))
            {
                const OpRcPtr & first = opVec[firstindex];
                const OpRcPtr & second = opVec[firstindex+1];
                
                // The common case of inverse ops is to have a deep nesting:
                // ..., A, B, B', A', ...
                //
                // Consider the above, when firstindex reaches B:
                //
                //         |
                // ..., A, B, B', A', ...
                //
                // We will remove B and B'.
                // Firstindex remains pointing at the original location:
                // 
                //         |
                // ..., A, A', ...
                //
                // We then decrement firstindex by 1,
                // to backstep and reconsider the A, A' case:
                //
                //      |            <-- firstindex decremented
                // ..., A, A', ...
                //
                
                if(first->isSameType(second) && first->isInverse(second))
                {
                    opVec.erase(opVec.begin() + firstindex,
                        opVec.begin() + firstindex + 2);
                    ++count;
                    
                    firstindex = std::max(0, firstindex-1);
                }
                else
                {
                    ++firstindex;
                }
            }
            
            return count;
        }
        
        /*
        int CombineOps(OpRcPtrVec & opVec)
        {
            int count = 0;
            int firstindex = 0; // this must be a signed int
            
            while(firstindex < static_cast<int>(opVec.size()-1))
            {
                const OpRcPtr & first = opVec[firstindex];
                const OpRcPtr & second = opVec[firstindex+1];
                
                if(IsExponentOp(first) && IsExponentOp(second))
                {
                    OpRcPtr newop = CreateCombinedExponentOp(first, second);
                    if(!newop) throw Exception("Error combining ExponentOps.");
                    
                    opVec[firstindex] = newop;
                    opVec.erase(opVec.begin() + firstindex + 1);
                    
                    ++firstindex;
                    ++count;
                }
                
                // TODO: Add matrix combining
                else
                {
                    ++firstindex;
                }
            }
            
            return count;
        }
        */
    }
    
    void OptimizeOpVec(OpRcPtrVec & ops)
    {
        if(ops.empty()) return;
        
        
        if(IsDebugLoggingEnabled())
        {
            LogDebug("Optimizing Op Vec...");
            LogDebug(SerializeOpVec(ops, 4));
        }
        
        OpRcPtrVec::size_type originalSize = ops.size();
        int total_noops = 0;
        int total_inverseops = 0;
        int passes = 0;
        
        while(passes<=MAX_OPTIMIZATION_PASSES)
        {
            int noops = RemoveNoOps(ops);
            int inverseops = RemoveInverseOps(ops);
            if(noops == 0 && inverseops==0)
            {
                // No optimization progress was made, so stop trying.
                break;
            }
            
            total_noops += noops;
            total_inverseops += inverseops;
            
            ++passes;
        }
        
        OpRcPtrVec::size_type finalSize = ops.size();
        
        if(passes == MAX_OPTIMIZATION_PASSES)
        {
            std::ostringstream os;
            os << "The max number of passes, " << passes << ", ";
            os << "was reached during optimization. This is likely a sign ";
            os << "that either the complexity of the color transform is ";
            os << "very high, or that some internal optimizers are in conflict ";
            os << "(undo-ing / redo-ing the other's results).";
            LogDebug(os.str().c_str());
        }
        
        if(IsDebugLoggingEnabled())
        {
            std::ostringstream os;
            os << "Optimized ";
            os << originalSize << "->" << finalSize << ", ";
            os << passes << " passes, ";
            os << total_noops << " noops removed, ";
            os << total_inverseops << " inverse ops removed\n";
            os << SerializeOpVec(ops, 4);
            LogDebug(os.str());
        }
    }
}
OCIO_NAMESPACE_EXIT


///////////////////////////////////////////////////////////////////////////////

#ifdef OCIO_UNIT_TEST

namespace OCIO = OCIO_NAMESPACE;
#include "UnitTest.h"

#include "ExponentOps.h"
#include "LogOps.h"
#include "Lut1DOp.h"
#include "Lut3DOp.h"
#include "MatrixOps.h"

OIIO_ADD_TEST(OpOptimizers, RemoveInverseOps)
{
    OCIO::OpRcPtrVec ops;
    
    float exp[4] = { 1.2f, 1.3f, 1.4f, 1.5f };
    
    
    float k[3] = { 0.18f, 0.18f, 0.18f };
    float m[3] = { 2.0f, 2.0f, 2.0f };
    float b[3] = { 0.1f, 0.1f, 0.1f };
    float base[3] = { 10.0f, 10.0f, 10.0f };
    float kb[3] = { 1.0f, 1.0f, 1.0f };
    
    OCIO::CreateExponentOp(ops, exp, OCIO::TRANSFORM_DIR_FORWARD);
    OCIO::CreateLogOp(ops, k, m, b, base, kb, OCIO::TRANSFORM_DIR_FORWARD);
    OCIO::CreateLogOp(ops, k, m, b, base, kb, OCIO::TRANSFORM_DIR_INVERSE);
    OCIO::CreateExponentOp(ops, exp, OCIO::TRANSFORM_DIR_INVERSE);
    
    OIIO_CHECK_EQUAL(ops.size(), 4);
    OCIO::RemoveInverseOps(ops);
    OIIO_CHECK_EQUAL(ops.size(), 0);
    
    
    ops.clear();
    OCIO::CreateExponentOp(ops, exp, OCIO::TRANSFORM_DIR_FORWARD);
    OCIO::CreateExponentOp(ops, exp, OCIO::TRANSFORM_DIR_INVERSE);
    OCIO::CreateLogOp(ops, k, m, b, base, kb, OCIO::TRANSFORM_DIR_INVERSE);
    OCIO::CreateLogOp(ops, k, m, b, base, kb, OCIO::TRANSFORM_DIR_FORWARD);
    OCIO::CreateExponentOp(ops, exp, OCIO::TRANSFORM_DIR_FORWARD);
    
    OIIO_CHECK_EQUAL(ops.size(), 5);
    OCIO::RemoveInverseOps(ops);
    OIIO_CHECK_EQUAL(ops.size(), 1);
}

#endif // OCIO_UNIT_TEST
