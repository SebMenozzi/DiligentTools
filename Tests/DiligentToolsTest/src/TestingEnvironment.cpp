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

#include "TestingEnvironment.hpp"

namespace Diligent
{

namespace Testing
{

TestingEnvironment::TestingEnvironment()
{
    SetDebugMessageCallback(MessageCallback);
}

TestingEnvironment::~TestingEnvironment()
{
    SetDebugMessageCallback(nullptr);
}

TestingEnvironment* TestingEnvironment::Initialize(int argc, char** argv)
{
    m_pTheEnvironment = new TestingEnvironment{};
    return m_pTheEnvironment;
}

TestingEnvironment* TestingEnvironment::GetInstance() { return m_pTheEnvironment; }

void TestingEnvironment::SetErrorAllowance(int NumErrorsToAllow, const char* InfoMessage)
{
    m_NumAllowedErrors = NumErrorsToAllow;
    if (InfoMessage != nullptr)
        std::cout << InfoMessage;
    if (m_NumAllowedErrors == 0)
    {
        std::unique_lock<std::mutex> Lock{m_ExpectedErrorSubstringsMtx};
        m_ExpectedErrorSubstrings.clear();
    }
}


void TestingEnvironment::PushExpectedErrorSubstring(const char* Str, bool ClearStack)
{
    if (ClearStack)
        m_ExpectedErrorSubstrings.clear();
    VERIFY_EXPR(Str != nullptr && Str[0] != '\0');
    m_ExpectedErrorSubstrings.push_back(Str);
}

void TestingEnvironment::MessageCallback(DEBUG_MESSAGE_SEVERITY Severity, const Char* Message, const char* Function, const char* File, int Line)
{
    if (Severity == DEBUG_MESSAGE_SEVERITY_ERROR || Severity == DEBUG_MESSAGE_SEVERITY_FATAL_ERROR)
    {
        if (m_NumAllowedErrors == 0)
        {
            ADD_FAILURE() << "Unexpected error";
        }
        else
        {
            m_NumAllowedErrors--;
            std::unique_lock<std::mutex> Lock{m_ExpectedErrorSubstringsMtx};
            if (!m_ExpectedErrorSubstrings.empty())
            {
                const auto& ErrorSubstring = m_ExpectedErrorSubstrings.back();
                if (strstr(Message, ErrorSubstring.c_str()) == nullptr)
                    ADD_FAILURE() << "Expected error substring '" << ErrorSubstring << "' was not found in the error message";
                m_ExpectedErrorSubstrings.pop_back();
            }
        }
    }

    PlatformDebug::OutputDebugMessage(Severity, Message, Function, File, Line);
}

TestingEnvironment*      TestingEnvironment::m_pTheEnvironment = nullptr;
std::atomic_int          TestingEnvironment::m_NumAllowedErrors{0};
std::vector<std::string> TestingEnvironment::m_ExpectedErrorSubstrings{};
std::mutex               TestingEnvironment::m_ExpectedErrorSubstringsMtx{};

} // namespace Testing
} // namespace Diligent
