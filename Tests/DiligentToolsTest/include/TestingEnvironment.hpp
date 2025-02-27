/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "DebugUtilities.hpp"
#include "PlatformDebug.hpp"

#include "gtest/gtest.h"

namespace Diligent
{

namespace Testing
{

class TestingEnvironment : public ::testing::Environment
{
public:
    TestingEnvironment();

    ~TestingEnvironment() override;

    static TestingEnvironment* Initialize(int argc, char** argv);

    static TestingEnvironment* GetInstance();

    static void SetErrorAllowance(int NumErrorsToAllow, const char* InfoMessage = nullptr);

    static void PushExpectedErrorSubstring(const char* Str, bool ClearStack = true);

protected:
    static void MessageCallback(DEBUG_MESSAGE_SEVERITY Severity, const Char* Message, const char* Function, const char* File, int Line);

    static TestingEnvironment*      m_pTheEnvironment;
    static std::atomic_int          m_NumAllowedErrors;
    static std::vector<std::string> m_ExpectedErrorSubstrings;
    static std::mutex               m_ExpectedErrorSubstringsMtx;
};

struct TestingEnvironmentScope
{
    TestingEnvironmentScope(const std::initializer_list<const char*>& Messages)
    {
        auto* pTestEnvironment = TestingEnvironment::GetInstance();
        pTestEnvironment->SetErrorAllowance(static_cast<Int32>(Messages.size()));

        for (auto const& Message : Messages)
            pTestEnvironment->PushExpectedErrorSubstring(Message, false);
    }

    ~TestingEnvironmentScope()
    {
        auto* pTestEnvironment = TestingEnvironment::GetInstance();
        pTestEnvironment->SetErrorAllowance(0);
    }
};


} // namespace Testing

} // namespace Diligent
