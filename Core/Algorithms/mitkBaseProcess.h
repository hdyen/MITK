/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Module:    $RCSfile$
Language:  C++
Date:      $Date$
Version:   $Revision$

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/


#ifndef BASEPROCESS_H_HEADER_INCLUDED_C19BE6FC
#define BASEPROCESS_H_HEADER_INCLUDED_C19BE6FC

#if(_MSC_VER==1200)
#include <itkFixedArray.h>
#endif

#include "mitkCommon.h"
#include <itkProcessObject.h>

namespace mitk {

class BaseData;

//##ModelId=3DCBF9BC011A
//##Documentation
//## @brief Superclass of all classes generating some kind of mitk::BaseData.
//## Superclass of all classes generating some kind of mitk::BaseData.
//## @ingroup Process
//## In itk and vtk the generated result of a ProcessObject is only guaranteed
//## to be up-to-date, when Update() of the ProcessObject or the generated
//## DataObject is called immediately before access of the data stored in the
//## DataObject. This is also true for subclasses of mitk::BaseProcess. But
//## many of the subclasses of mitk::BaseProcess define additional access
//## functions to the generated output that guarantee an up-to-date result, see
//## for example mitk::ImageSource. 
class BaseProcess : public itk::ProcessObject
{
public:
    mitkClassMacro(BaseProcess, itk::ProcessObject);

    //##Documentation
    //## @brief Access itk::ProcessObject::m_Updating
    //##
    //## m_Updating indicates when the pipeline is executing.
    //## It prevents infinite recursion when pipelines have loops.
    //## \sa itk::ProcessObject::m_Updating
    bool Updating() const
    {
      return m_Updating;
    }

    //##ModelId=3E8600DC03E2
    //##Documentation
    //## @brief Helps to deal with the weak-pointer-problem.
    virtual void UnRegister() const;

    //##ModelId=3E8600DD000E
    //##Documentation
    //## @brief Helps to deal with the weak-pointer-problem.
    virtual int GetExternalReferenceCount() const;
protected:
    //##ModelId=3E8600DD0036
    BaseProcess();
    //##ModelId=3E8600DD004A
    virtual ~BaseProcess();

    //##ModelId=3E8600DD0072
    //##Documentation
    //## @brief Protected methods for setting outputs.
    //## 
    //## Subclasses make use of them for getting output. 
    //## These are only overwritten because of itk::DataObject::connectSource being 
    //## private and non-virtual: the important stuff is done in 
    //## mitk::BaseData::connectSource.
    virtual void SetNthOutput(unsigned int num, itk::DataObject *output);
    //##ModelId=3E8600DD00F4
    //##Documentation
    //## @brief Protected methods for setting outputs.
    //## 
    //## Subclasses make use of them for getting output. 
    //## These are only overwritten because of itk::DataObject::connectSource being 
    //## private and non-virtual: the important stuff is done in 
    //## mitk::BaseData::connectSource.
    virtual void AddOutput(itk::DataObject *output);

private:
    //##ModelId=3E8600DC0356
    //##Documentation
    //## @brief Helps to deal with the weak-pointer-problem.
    mutable bool m_Unregistering;
    //##Documentation
    //## @brief Helps to deal with the weak-pointer-problem.
    //##ModelId=3E8600DC037E
    mutable bool m_CalculatingExternalReferenceCount;
    //##Documentation
    //## @brief Helps to deal with the weak-pointer-problem.
    //##ModelId=3E8600DC03B0
    mutable int m_ExternalReferenceCount;
};

} // namespace mitk



#endif /* BASEPROCESS_H_HEADER_INCLUDED_C19BE6FC */
