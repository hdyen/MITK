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


#include "mitkDataTreeNode.h"
#include "mitkMapperFactory.h"
#include <vtkTransform.h>

#include "mitkProperties.h"
#include "mitkStringProperty.h"
#include "mitkColorProperty.h"
#include "mitkLevelWindowProperty.h"
#include "mitkGeometry3D.h"

//##ModelId=3D6A0E8C02CC
mitk::Mapper* mitk::DataTreeNode::GetMapper(MapperSlotId id) const
{
  if( (id >= m_Mappers.size()) || (m_Mappers[id].IsNull()) ) 
  {
    if(id >= m_Mappers.capacity())
    {
//      int i, size=id-m_Mappers.capacity()+10;
      m_Mappers.resize(id+10);
    }
    m_Mappers[id] = MapperFactory::CreateMapper(const_cast<DataTreeNode*>(this),id);
  }
  return m_Mappers[id];
}

//##ModelId=3E32C49D00A8
mitk::BaseData* mitk::DataTreeNode::GetData() const
{
  return m_Data;
}

mitk::Interactor::Pointer mitk::DataTreeNode::GetInteractor() const
{
  return m_Interactor;
}

//##ModelId=3E33F4E4025B
void mitk::DataTreeNode::SetData(mitk::BaseData* baseData)
{
  if(m_Data!=baseData)
  {
    m_Data=baseData;

    m_Mappers.clear();
    m_Mappers.resize(10);

    m_DataReferenceChangedTime.Modified();
    Modified();
  }
}

void mitk::DataTreeNode::SetInteractor(mitk::Interactor* interactor)
{
  m_Interactor = interactor;
}

//##ModelId=3E33F5D702AA
mitk::DataTreeNode::DataTreeNode() : m_Data(NULL)
{
  m_Mappers.resize(10);

  m_PropertyList = PropertyList::New();
}


//##ModelId=3E33F5D702D3
mitk::DataTreeNode::~DataTreeNode()
{

}

//##ModelId=3E33F5D7032D
mitk::DataTreeNode& mitk::DataTreeNode::operator=(const DataTreeNode& right)
{
  mitk::DataTreeNode* node=mitk::DataTreeNode::New();
  node->SetData(right.GetData());
  return *node;
}

mitk::DataTreeNode& mitk::DataTreeNode::operator=(mitk::BaseData* right)
{
  mitk::DataTreeNode* node=mitk::DataTreeNode::New();
  node->SetData(right);
  return *node;
}

#if (_MSC_VER > 1200) || !defined(_MSC_VER)
MBI_STD::istream& mitk::operator>>( MBI_STD::istream& i, mitk::DataTreeNode::Pointer& dtn )
#endif
#if ((defined(_MSC_VER)) && (_MSC_VER <= 1200))
MBI_STD::istream& operator>>( MBI_STD::istream& i, mitk::DataTreeNode::Pointer& dtn ) 
#endif
{
  dtn = mitk::DataTreeNode::New();
  //i >> av.get();
  return i;
}

#if (_MSC_VER > 1200) || !defined(_MSC_VER)
MBI_STD::ostream& mitk::operator<<( MBI_STD::ostream& o, mitk::DataTreeNode::Pointer& dtn)
#endif
#if ((defined(_MSC_VER)) && (_MSC_VER <= 1200))
MBI_STD::ostream& operator<<( MBI_STD::ostream& o, mitk::DataTreeNode::Pointer& dtn)
#endif
{
  if(dtn->GetData()!=NULL)
    o<<dtn->GetData()->GetNameOfClass();
  else
    o<<"empty data";
  return o;
}

//##ModelId=3E69331903C9
void mitk::DataTreeNode::SetMapper(MapperSlotId id, mitk::Mapper* mapper)
{
  m_Mappers[id] = mapper;

  if (mapper!=NULL)
    mapper->SetDataTreeNode(this);
}

//##ModelId=3E860A5C0032
void mitk::DataTreeNode::UpdateOutputInformation()
{
  if (this->GetSource())
  {
    this->GetSource()->UpdateOutputInformation();
  }
}

//##ModelId=3E860A5E011B
void mitk::DataTreeNode::SetRequestedRegionToLargestPossibleRegion()
{
}

//##ModelId=3E860A5F03D9
bool mitk::DataTreeNode::RequestedRegionIsOutsideOfTheBufferedRegion()
{
  return false;
}

//##ModelId=3E860A620080
bool mitk::DataTreeNode::VerifyRequestedRegion()
{
    return true;
}

//##ModelId=3E860A640156
void mitk::DataTreeNode::SetRequestedRegion(itk::DataObject *data)
{
}

//##ModelId=3E860A6601DB
void mitk::DataTreeNode::CopyInformation(const itk::DataObject *data)
{
}
//##ModelId=3E3FE0420273
mitk::PropertyList::Pointer mitk::DataTreeNode::GetPropertyList(const mitk::BaseRenderer* renderer) const
{
  if(renderer==NULL)
    return m_PropertyList;

  mitk::PropertyList::Pointer & propertyList = m_MapOfPropertyLists[renderer];

  if(propertyList.IsNull())
    propertyList = mitk::PropertyList::New();

  assert(m_MapOfPropertyLists[renderer].IsNotNull());

  return propertyList;
}

//##ModelId=3EF189DB0111
mitk::BaseProperty::Pointer mitk::DataTreeNode::GetProperty(const char *propertyKey, const mitk::BaseRenderer* renderer) const
{
  if(propertyKey==NULL)
    return NULL;

  std::map<const mitk::BaseRenderer*,mitk::PropertyList::Pointer>::const_iterator it;

  //does a renderer-specific PropertyList exist?
  it=m_MapOfPropertyLists.find(renderer);
  if(it==m_MapOfPropertyLists.end())
    //no? use the renderer-independent one!
    return m_PropertyList->GetProperty(propertyKey);

  //does the renderer-specific PropertyList contain the @a propertyKey?
  //and is it enabled
  mitk::BaseProperty::Pointer property;
  property=it->second->GetProperty(propertyKey);
  if(property.IsNotNull() && property->GetEnabled())
    //yes? return it
    return property;

  //no? use the renderer-independent one!
  property=m_PropertyList->GetProperty(propertyKey);
  if(property.IsNotNull() && property->GetEnabled())
    return property;
  return NULL;
}

bool mitk::DataTreeNode::GetBoolProperty(const char* propertyKey, bool& boolValue, mitk::BaseRenderer* renderer) const
{
  mitk::BoolProperty::Pointer boolprop = dynamic_cast<mitk::BoolProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(boolprop.IsNull())
    return false;

  boolValue = boolprop->GetValue();
  return true;
}

bool mitk::DataTreeNode::GetIntProperty(const char* propertyKey, int &intValue, mitk::BaseRenderer* renderer) const
{
  mitk::IntProperty::Pointer intprop = dynamic_cast<mitk::IntProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(intprop.IsNull())
    return false;

  intValue = intprop->GetValue();
  return true;
}

bool mitk::DataTreeNode::GetStringProperty(const char* propertyKey, const char* string, mitk::BaseRenderer* renderer) const
{
  mitk::StringProperty::Pointer stringProp = dynamic_cast<mitk::StringProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(stringProp.IsNull())
  {
    return false;
  } 
  else 
  {
    memcpy((void*)string, stringProp->GetValue(), strlen(stringProp->GetValue()) + 1 );
    return true;
  }
}

bool mitk::DataTreeNode::GetColor(float rgb[3], mitk::BaseRenderer* renderer, const char* propertyKey) const
{
  mitk::ColorProperty::Pointer colorprop = dynamic_cast<mitk::ColorProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(colorprop.IsNull())
    return false;

  memcpy(rgb, colorprop->GetColor().GetDataPointer(), 3*sizeof(float));
  return true;
}

//##ModelId=3EF19420016B
bool mitk::DataTreeNode::GetOpacity(float &opacity, mitk::BaseRenderer* renderer, const char* propertyKey) const
{
  mitk::FloatProperty::Pointer opacityprop = dynamic_cast<mitk::FloatProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(opacityprop.IsNull())
    return false;

  opacity=opacityprop->GetValue();
  return true;
}

//##ModelId=3EF194220204
bool mitk::DataTreeNode::GetLevelWindow(mitk::LevelWindow &levelWindow, mitk::BaseRenderer* renderer, const char* propertyKey) const
{
  mitk::LevelWindowProperty::Pointer levWinProp = dynamic_cast<mitk::LevelWindowProperty*>(GetProperty(propertyKey, renderer).GetPointer());
  if(levWinProp.IsNull())
    return false;

  levelWindow=levWinProp->GetLevelWindow();
  return true;
}

void mitk::DataTreeNode::SetColor(const mitk::Color &color, mitk::BaseRenderer* renderer, const char* propertyKey)
{
  mitk::ColorProperty::Pointer prop;
  prop = new mitk::ColorProperty(color);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

void mitk::DataTreeNode::SetColor(float red, float green, float blue, mitk::BaseRenderer* renderer, const char* propertyKey)
{
  float color[3];
  color[0]=red;
  color[1]=green;
  color[2]=blue;
  SetColor(color, renderer, propertyKey);
}

//##ModelId=3EF196360303
void mitk::DataTreeNode::SetColor(const float rgb[3], mitk::BaseRenderer* renderer, const char* propertyKey)
{
  mitk::ColorProperty::Pointer prop;
  prop = new mitk::ColorProperty(rgb);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

//##ModelId=3EF1966703D6
void mitk::DataTreeNode::SetVisibility(bool visible, mitk::BaseRenderer* renderer, const char* propertyKey)
{
  mitk::BoolProperty::Pointer prop;
  prop = new mitk::BoolProperty(visible);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

//##ModelId=3EF196880095
void mitk::DataTreeNode::SetOpacity(float opacity, mitk::BaseRenderer* renderer, const char* propertyKey)
{
  mitk::FloatProperty::Pointer prop;
  prop = new mitk::FloatProperty(opacity);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

//##ModelId=3EF1969A0181
void mitk::DataTreeNode::SetLevelWindow(mitk::LevelWindow levelWindow, mitk::BaseRenderer* renderer, const char* propertyKey)
{
  mitk::LevelWindowProperty::Pointer prop;
  prop = new mitk::LevelWindowProperty(levelWindow);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

void mitk::DataTreeNode::SetIntProperty(const char* propertyKey, int intValue, mitk::BaseRenderer* renderer)
{
  mitk::IntProperty::Pointer prop;
  prop = new mitk::IntProperty(intValue);
  GetPropertyList(renderer)->SetProperty(propertyKey, prop);
}

void mitk::DataTreeNode::SetProperty(const char *propertyKey, 
                                     BaseProperty* propertyValue, 
                                     const mitk::BaseRenderer* renderer)
{
  GetPropertyList(renderer)->SetProperty(propertyKey, propertyValue);
}

//##ModelId=3ED91D050121
vtkLinearTransform* mitk::DataTreeNode::GetVtkTransform() const
{
  assert(m_Data.IsNotNull());
  assert(m_Data->GetGeometry()!=NULL);
  return m_Data->GetGeometry()->GetVtkTransform();
}

unsigned long mitk::DataTreeNode::GetMTime() const
{
  unsigned long time = Superclass::GetMTime();
  if(m_Data.IsNotNull())
	{
    if((time < m_Data->GetMTime()) ||
      ((m_Data->GetSource() != NULL) && (time < m_Data->GetSource()->GetMTime()))
    )
		  Modified();
	}
  return Superclass::GetMTime();
}
