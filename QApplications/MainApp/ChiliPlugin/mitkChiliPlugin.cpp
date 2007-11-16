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

//Chili
#include <ipDicom/ipDicom.h>  //read DICOM-Files
#include <chili/cdbTypes.h>  //series_t*, study_t*, ...
#include <chili/qclightboxmanager.h>  //get newLightbox, currentLightbox
#include <ipPic/ipPic.h>  //ipPicDescriptor
#include <ipPic/ipPicTags.h>  //Tags
//MITK-Plugin
#include "mitkChiliPlugin.h"
#include "mitkChiliPluginEvents.h"
#include "mitkChiliPluginFactory.h"
#include "mitkPicDescriptorToNode.h"
#include "mitkImageNumberFilter.h"
#include "mitkSingleSpacingFilter.h"
#include "mitkSpacingSetFilter.h"
#include "QmitkChiliPluginSaveDialog.h"
#include "mitkLightBoxImageReader.h"  //TODO entfernen wenn das neue Chili-Release installiert ist
#include "mitkChiliMacros.h"

//MITK
#include <mitkCoreObjectFactory.h>
#include <QmitkStdMultiWidget.h>
#include <QmitkEventCollector.h>
#include <QcMITKTask.h>
#include <SampleApp.h>
//teleconference
#include <mitkConferenceToken.h>
#include <mitkConferenceEventMapper.h>
#include <QmitkChili3ConferenceKitFactory.h>
//QT
#include <qlayout.h>
#include <qapplication.h>
#include <qeventloop.h>
#include <qcursor.h>
#include <qpixmap.h>
#include <qmessagebox.h>
#include <qtooltip.h>
#include <qtimer.h>
#include <qobjectlist.h>
#include <qprogressbar.h>

#include <mitk_chili_plugin.xpm>
#include "chili_lightbox_import.xpm"
#include "chili_restart_mitk.xpm"

QWidget* mitk::ChiliPlugin::s_Parent = NULL;
/** Create event-logging object (ConferenceKit). */
Chili3ConferenceKitFactory chiliconferencekitfactory;

/** Entry point for Chili. */
extern "C"
QcEXPORT QObject* create( QWidget *parent )
{
  // save parent for parameter-less constructor
  mitk::ChiliPlugin::SetQtParent( parent );

  // overwrite implementation of mitk::ChiliPlugin with mitk::ChiliPlugin
  mitk::ChiliPluginFactory::RegisterOneFactory();

  return mitk::ChiliPlugin::GetQcPluginInstance();
}

/** Constructor with hidden parameter s_Parent. */
mitk::ChiliPlugin::ChiliPlugin()
: QcPlugin( s_Parent ),
  app( NULL ),
  m_UseReader( 0 ),
  m_LightBoxCount( 0 ),
  m_InImporting (false)
{
  task = new QcMITKTask( xpm(), s_Parent, name() );

  EventCollector* logger = new EventCollector();
  qApp->installEventFilter(logger);

  m_tempDirectory = GetTempDirectory();
  if( m_tempDirectory.empty() )
    QMessageBox::information( 0, "MITK", "MITK was not able to create a Temp-Directory.\nYou can only Load via Lightbox.\nMore should not be possible." );
  else
    std::cout << "ChiliPlugin: Create and use directory "<< m_tempDirectory << std::endl;
}

/** Destructor */
mitk::ChiliPlugin::~ChiliPlugin()
{
  #ifdef WIN32
    std::string removeDirectory = "rm /s " + m_tempDirectory;
    std::cout << "ChiliPlugin: Delete directory "<< m_tempDirectory << std::endl;
  #else
    std::string removeDirectory = "rm -r " + m_tempDirectory;
    std::cout << "ChiliPlugin: Delete directory "<< m_tempDirectory << std::endl;
  #endif

  system( removeDirectory.c_str() );

  task->deleteLater();

  // tricky tricky...
  m_ReferenceCountLock.Lock();
  ++m_ReferenceCount; // increase count, so the following notice to mitk::ChiliPlugin won't trigger another delete on "this"
  m_ReferenceCountLock.Unlock();

  PACSPlugin::GetInstance(true); // set internal s_Instance = NULL, so ITK won't have another SmartPointer to us (hopefully nobody else has)

  m_ReferenceCountLock.Lock();
  m_ReferenceCount = 0; // set count to 0, so itk::LightObject won't trigger another delete...
  m_ReferenceCountLock.Unlock();
}

QString mitk::ChiliPlugin::name()
{
  return QString( "MITK" );
}

const char** mitk::ChiliPlugin::xpm()
{
  return (const char **)mitk_chili_plugin_xpm;
}

/** Set the qtparent, needed for QObject* create(). */
void mitk::ChiliPlugin::SetQtParent( QWidget* parent )
{
  s_Parent = parent;
}

/** Create a new SampleApp. */
void mitk::ChiliPlugin::CreateSampleApp()
{
  static bool done = false;
  if( !done ) // this is done here, beause if it's done in the constructor, then many button labels get wrong... (qt names as labels)
  {
    // toplevel layout
    horzlayout = new QHBoxLayout( task ); // member
    QVBoxLayout* vertlayout = new QVBoxLayout( horzlayout );

    // 4 lightbox import buttons
    for (unsigned int row = 0; row < NUMBER_OF_THINKABLE_LIGHTBOXES; ++row)
    {
      QIDToolButton* toolbutton = new QIDToolButton( row, task );
      toolbutton->setUsesTextLabel( true );
      QToolTip::add( toolbutton, "Import this lightbox to MITK");
      toolbutton->setTextPosition( QToolButton::BelowIcon );
      toolbutton->setPixmap( QPixmap(chili_lightbox_import_xpm) );

      QSizePolicy policy = toolbutton->sizePolicy();
      policy.setVerStretch( 2 );
      toolbutton->setSizePolicy( policy );

      connect( toolbutton, SIGNAL(clicked(int)), this, SLOT(lightBoxImportButtonClicked(int)));

      m_LightBoxImportButtons.push_back( toolbutton ); // we need to know these for import of lightboxes
      vertlayout->addWidget( toolbutton );

      if (row < m_LightBoxCount)
      {
        toolbutton->show();
      }
      else
      {
        toolbutton->hide();
      }
    }

    // button for application restart
    m_LightBoxImportToggleButton = new QToolButton( task );
    m_LightBoxImportToggleButton->setText(" "); // just some space that generates one short empty line of text
    m_LightBoxImportToggleButton->setPixmap( QPixmap(chili_restart_mitk_xpm) );
    m_LightBoxImportToggleButton->setAutoRaise(true);
    m_LightBoxImportToggleButton->setEnabled(true); //!! secret button to reinit application
    connect( m_LightBoxImportToggleButton, SIGNAL(clicked()), this, SLOT(OnApproachReinitializationOfWholeApplication()) );
    QSizePolicy policy = m_LightBoxImportToggleButton->sizePolicy();
    policy.setVerStretch( 1 );
    m_LightBoxImportToggleButton->setSizePolicy( m_LightBoxImportToggleButton->sizePolicy());
    vertlayout->addWidget( m_LightBoxImportToggleButton );
  }

  if (app)
  {
    // delete old application
    horzlayout->remove(app);
    delete app;
  }

  // create one instance of SampleApp
  app = new SampleApp( task, "sample", 0 );
  horzlayout->addWidget( app ); 
  horzlayout->activate();
  app->show();

  if (!done)
  {
    done = true;
  }

  SendStudySelectedEvent();
}

/** Return the plugininstance. */
QcPlugin* mitk::ChiliPlugin::GetQcPluginInstance()
{
  // give Chili the single instance of mitk::ChiliPlugin
  PACSPlugin::Pointer pluginInstance = PACSPlugin::GetInstance();
  ChiliPlugin::Pointer realPluginInstance = dynamic_cast<ChiliPlugin*>( pluginInstance.GetPointer() );

  static bool done = false;

  if (!done)
  {
    QTimer* timer = new QTimer(realPluginInstance);
    connect( timer, SIGNAL(timeout()), realPluginInstance, SLOT(CreateSampleApp()) );
    timer->start(1000, true);

    done = true;
  }

  return realPluginInstance.GetPointer();
}

/** Return the ConferenceID. */
int mitk::ChiliPlugin::GetConferenceID()
{
  return m_QmitkChiliPluginConferenceID;
}

/** Return true if the application run as plugin and false if its standalone. */
bool mitk::ChiliPlugin::IsPlugin()
{
  return true;
}

/** STUDY-, PATIENT-, SERIES- AND TEXT-INFORMATIONS */

/** Return the studyinformation. If you dont set the seriesOID, you get the current selected study. If you want a specific study, set the OID. If no study could found, this function return StudyInformation.OID == "". */
mitk::PACSPlugin::StudyInformation mitk::ChiliPlugin::GetStudyInformation( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  StudyInformation resultInformation;
  resultInformation.OID = "";

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultInformation;

#else

  study_t study;
  series_t series;
  initStudyStruct( &study );
  initSeriesStruct( &series );

  if( seriesOID == "" )
  {
    //use current selected study
    if( pCurrentStudy() != NULL )
      study = (*dupStudyStruct( pCurrentStudy() ) );  //copy the StudyStruct
    else
    {
      std::cout << "ChiliPlugin (GetStudyInformation): pCurrentStudy() failed. Abort." << std::endl;
      clearStudyStruct( &study );
      clearSeriesStruct( &series );
      return resultInformation;
    }
  }
  else
  {
    //let chili search for study
    series.oid = strdup( seriesOID.c_str() );
    if( !pQuerySeries( this, &series, &study, NULL ) )
    {
      clearStudyStruct( &study );
      clearSeriesStruct( &series );
      std::cout << "ChiliPlugin (GetStudyInformation): pQueryStudy() failed. Abort." << std::endl;
      return resultInformation;
    }
  }

  resultInformation.OID = study.oid;
  resultInformation.InstanceUID = study.instanceUID;
  resultInformation.ID = study.id;
  resultInformation.Date = study.date;
  resultInformation.Time = study.time;
  resultInformation.Modality = study.modality;
  resultInformation.Manufacturer = study.manufacturer;
  resultInformation.ReferingPhysician = study.referingPhysician;
  resultInformation.Description = study.description;
  resultInformation.ManufacturersModelName = study.manufacturersModelName;
  resultInformation.AccessionNumber = study.accessionNumber;
  resultInformation.InstitutionName = study.institutionName;
  resultInformation.PerformingPhysician = study.performingPhysician;
  resultInformation.ReportingPhysician = study.reportingPhysician;
  resultInformation.LastAccess = study.last_access;
  resultInformation.ImageCount = study.image_count;

  clearStudyStruct( &study );
  clearSeriesStruct( &series );
  return resultInformation;
#endif
}

/** Return the patientinformation. If you dont set the seriesOID, you get the current selected patient. If you want a specific patient, set the OID. If no patient could found, this function return PatientInformation.OID == "". */
mitk::PACSPlugin::PatientInformation mitk::ChiliPlugin::GetPatientInformation( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  PatientInformation resultInformation;
  resultInformation.OID = "";

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultInformation;

#else

  patient_t patient;
  study_t study;
  series_t series;
  initStudyStruct( &study );
  initSeriesStruct( &series );
  initPatientStruct( &patient );

  if( seriesOID == "" )
  {
    //use current selected patient
    if( pCurrentPatient() != NULL )
      patient = (*dupPatientStruct( pCurrentPatient() ) );  //copy patientstruct
    else
    {
      std::cout << "ChiliPlugin (GetPatientInformation): pCurrentPatient() failed. Abort." << std::endl;
      clearPatientStruct( &patient );
      clearStudyStruct( &study );
      clearSeriesStruct( &series );
      return resultInformation;
    }
  }
  else
  {
    //let chili search for patient
    series.oid = strdup( seriesOID.c_str() );
    if( !pQuerySeries( this, &series, &study, &patient ) )
    {
      clearPatientStruct( &patient );
      clearStudyStruct( &study );
      clearSeriesStruct( &series );
      std::cout << "ChiliPlugin (GetPatientInformation): pQueryPatient() failed. Abort." << std::endl;
      return resultInformation;
    }
  }

  resultInformation.OID = patient.oid;
  resultInformation.Name = patient.name;
  resultInformation.ID = patient.id;
  resultInformation.BirthDate = patient.birthDate;
  resultInformation.BirthTime = patient.birthTime;
  resultInformation.Sex = patient.sex;
  resultInformation.MedicalRecordLocator = patient.medicalRecordLocator;
  resultInformation.Comment = patient.comment;

  clearStudyStruct( &study );
  clearSeriesStruct( &series );
  clearPatientStruct( &patient );
  return resultInformation;
#endif
}

/** Return the seriesinformation. If you dont set the seriesOID, you get the current selected series. If you want a specific series, set the OID. If no series could found, this function return SeriesInformation.OID == "". */
mitk::PACSPlugin::SeriesInformation mitk::ChiliPlugin::GetSeriesInformation( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  SeriesInformation resultInformation;
  resultInformation.OID = "";

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultInformation;

#else

  series_t series;
  initSeriesStruct( &series );

  if( seriesOID == "" )
  {
    //use current selected series
    if( pCurrentSeries() != NULL )
      series = (*dupSeriesStruct( pCurrentSeries() ) );  //copy seriesstruct
    else
    {
      std::cout << "ChiliPlugin (GetSeriesInformation): pCurrentSeries() failed. Abort." << std::endl;
      return resultInformation;
    }
  }
  else
  {
    //let chili search for series
    series.oid = strdup( seriesOID.c_str() );
    if( !pQuerySeries( this, &series, NULL, NULL ) )
    {
      clearSeriesStruct( &series );
      std::cout << "ChiliPlugin (GetSeriesInformation): pQuerySeries() failed. Abort." << std::endl;
      return resultInformation;
    }
  }

  resultInformation.OID = series.oid;
  resultInformation.InstanceUID = series.instanceUID;
  resultInformation.Number = series.number;
  resultInformation.Acquisition = series.acquisition;
  resultInformation.EchoNumber = series.echoNumber;
  resultInformation.TemporalPosition = series.temporalPosition;
  resultInformation.Date = series.date;
  resultInformation.Time = series.time;
  resultInformation.Description = series.description;
  resultInformation.Contrast = series.contrast;
  resultInformation.BodyPartExamined = series.bodyPartExamined;
  resultInformation.ScanningSequence = series.scanningSequence;
  resultInformation.FrameOfReferenceUID = series.frameOfReferenceUID;
  resultInformation.ImageCount = series.image_count;

  clearSeriesStruct( &series );
  return resultInformation;
#endif
}

/** Return the seriesinformationlist. If you dont set the studyOID, you get the seriesList of the current selected study. If you want a list of a specific study, set the OID. If no series could found, this function returns an empty list. */
mitk::PACSPlugin::SeriesInformationList mitk::ChiliPlugin::GetSeriesInformationList( const std::string& studyOID )
{
  //get used to save all found series
  m_SeriesInformationList.clear();
  //iterate over all series from study
  if( studyOID == "" )
    iterateSeries( (char*)GetStudyInformation().OID.c_str(), NULL, &ChiliPlugin::GlobalIterateSeriesForCompleteInformation, this );
  else
    iterateSeries( (char*)studyOID.c_str(), NULL, &ChiliPlugin::GlobalIterateSeriesForCompleteInformation, this );
  //the function filled the SeriesInformationList
  return m_SeriesInformationList;
}

/** This function Iterate over all series of one study and save the series-properties to m_SeriesInformationList. */
ipBool_t mitk::ChiliPlugin::GlobalIterateSeriesForCompleteInformation( int /*rows*/, int /*row*/, series_t* series, void* user_data )
{
  mitk::ChiliPlugin* callingObject = static_cast<mitk::ChiliPlugin*>(user_data);
  //create new element
  mitk::PACSPlugin::SeriesInformation newSeries;
  //fill element
  newSeries.OID = series->oid;
  newSeries.InstanceUID = series->instanceUID;
  newSeries.Number = series->number;
  newSeries.Acquisition = series->acquisition;
  newSeries.EchoNumber = series->echoNumber;
  newSeries.TemporalPosition = series->temporalPosition;
  newSeries.Date = series->date;
  newSeries.Time = series->time;
  newSeries.Description = series->description;
  newSeries.Contrast = series->contrast;
  newSeries.BodyPartExamined = series->bodyPartExamined;
  newSeries.ScanningSequence = series->scanningSequence;
  newSeries.FrameOfReferenceUID = series->frameOfReferenceUID;
#ifdef CHILI_PLUGIN_VERSION_CODE
  newSeries.ImageCount = series->image_count;
#else
  newSeries.ImageCount = 0;
#endif
  //add to list
  callingObject->m_SeriesInformationList.push_back( newSeries );

  return ipTrue; // enum from ipTypes.h
}

/** Return the textinformation. You have to set the textOID to get the information. If no text could found, this function return TextInformation.OID == "". */
mitk::PACSPlugin::TextInformation mitk::ChiliPlugin::GetTextInformation( const std::string& mitkHideIfNoVersionCode(textOID) )
{
  TextInformation resultInformation;
  resultInformation.OID = "";

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultInformation;

#else

  if( textOID == "" )
    return resultInformation;

  text_t text;
  initTextStruct( &text );

  //let chili search for series
  text.oid = strdup( textOID.c_str() );
  if( !pQueryText( this, &text, NULL, NULL, NULL ) )
  {
    clearTextStruct( &text );
    std::cout << "ChiliPlugin (GetTextInformation): pQueryText() failed. Abort." << std::endl;
    return resultInformation;
  }

  resultInformation.OID = text.oid;
  resultInformation.MimeType = text.mime_type;
  resultInformation.ChiliText = text.chiliText;
  resultInformation.Status = text.status;
  resultInformation.FrameOfReferenceUID = text.frameOfReferenceUID;
  resultInformation.TextDate = text.text_date;
  resultInformation.Description = text.description;

  clearTextStruct( &text );
  return resultInformation;
#endif
}

/** Return a list of all textinformation. You have to set the seriesOID to get the textList. If no texts could found, this function returns an empty list. This function dont return the text which used to save and load the parent-child-relationship. */
mitk::PACSPlugin::TextInformationList mitk::ChiliPlugin::GetTextInformationList( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  //get used to save all found text
  m_TextInformationList.clear();
#ifdef CHILI_PLUGIN_VERSION_CODE
  //iterate over all text from series
  pIterateTexts( this, (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateTextForCompleteInformation, this );
  //the function filled the TextInformationList
#endif
  return m_TextInformationList;
}

#ifdef CHILI_PLUGIN_VERSION_CODE
/** This function Iterate over all texts of one series and save the text-properties to m_TextInformationList. */
ipBool_t mitk::ChiliPlugin::GlobalIterateTextForCompleteInformation( int rows, int row, text_t *text, void *user_data )
{
  mitk::ChiliPlugin* callingObject = static_cast<mitk::ChiliPlugin*>(user_data);

  //we dont want that the parent-child-relation-file can be load ;)
  std::string chiliDatabaseName = text->chiliText;
  std::string fileName = chiliDatabaseName.substr( chiliDatabaseName.find_last_of("-") + 1 );
  if( fileName != "ParentChild.xml" )
  {
    //create new element
    mitk::PACSPlugin::TextInformation resultText;
    //fill element
    resultText.OID = text->oid;
    resultText.MimeType = text->mime_type;
    resultText.ChiliText = text->chiliText;
    resultText.Status = text->status;
    resultText.FrameOfReferenceUID = text->frameOfReferenceUID;
    resultText.TextDate = text->text_date;
    resultText.Description = text->description;
    //add to list
    callingObject->m_TextInformationList.push_back( resultText );
  }
  return ipTrue; // enum from ipTypes.h
}
#endif

/** Return the number of current shown lightboxes and set by lightboxTiles(). */
unsigned int mitk::ChiliPlugin::GetLightboxCount()
{
  return m_LightBoxCount;
}

/** Return the first found empty chililightbox, using the lightboxmanager therefore. */
QcLightbox* mitk::ChiliPlugin::GetNewLightbox()
{
  QcLightbox* newLightbox;
  // chili can show maximal four lightboxes
  for( unsigned int i = 0; i < NUMBER_OF_THINKABLE_LIGHTBOXES; i++ )
  {
    newLightbox = lightboxManager()->getLightboxes().at(i);
    //return the first empty lightbox
    if( newLightbox->getFrames() == 0 )
    {
      newLightbox->activate();
      newLightbox->show();
      Modified();
      return newLightbox;
    }
  }
  QMessageBox::information( 0, "MITK", "You have reached the maximum count of Lightboxes. No one more can created." );
  return NULL;
}

/** Return the current selected chililightbox, using the lightboxmanager therefore. */
QcLightbox* mitk::ChiliPlugin::GetCurrentLightbox()
{
  QcLightbox* currentLightbox = lightboxManager()->getActiveLightbox();
  if( currentLightbox == NULL)
    QMessageBox::information( 0, "MITK", "There is no selected Lightbox in Chili." );
  return currentLightbox;
}

/** Set the internal reader which used to combine slices.
0: PicDescriptorToNode(ImageNumber);
1: PicDescriptorToNodeSecond(SpacingCombination);
2: PicDescriptorToNodeThird(most common spacing) */
void mitk::ChiliPlugin::SetReaderType( unsigned int readerType )
{
  m_UseReader = readerType;
}

/** LOAD-FUNCTIONS */

/** Load all images from the given lightbox. If no lightbox set, the current lightbox get used. Chili dont support text-access via lightbox. If you want to load them, use LoadAllTextsFromSeries(...). The slices get combined with the internal set ReaderType. */
std::vector<mitk::DataTreeNode::Pointer> mitk::ChiliPlugin::LoadImagesFromLightbox( QcLightbox* lightbox )
{
  std::vector<DataTreeNode::Pointer> resultVector;
  resultVector.clear();

#ifndef CHILI_PLUGIN_VERSION_CODE

  //QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  //return resultVector;

  //TODO entfernen wenn das neue Chili-Release installiert ist
  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );
  LightBoxImageReader::Pointer reader = LightBoxImageReader::New();
  reader->SetLightBox( lightbox );
  Image::Pointer image = reader->GetOutput();
  image->Update();
  if( image->IsInitialized() )
  //create node
  {
    DataTreeNode::Pointer node = DataTreeNode::New();
    node->SetData( image );
    DataTreeNodeFactory::SetDefaultImageProperties( node );
    mitk::PropertyList::Pointer tempPropertyList = reader->GetImageTagsAsPropertyList();
    //add properties to node
    for( mitk::PropertyList::PropertyMap::const_iterator iter = tempPropertyList->GetMap()->begin(); iter != tempPropertyList->GetMap()->end(); iter++ )
    {
      node->SetProperty( iter->first.c_str(), iter->second.first );
    }
    resultVector.push_back( node );
  }
  QApplication::restoreOverrideCursor();
  return resultVector;

#else

  if( lightbox == NULL )
  {
    lightbox = GetCurrentLightbox();
    if( lightbox == NULL )
      return resultVector;  //abort
  }

  if( lightbox->getFrames() > 0 )
  {
    QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

    std::list< ipPicDescriptor* > lightboxPicDescriptorList;
    lightboxPicDescriptorList.clear();

    PicDescriptorToNode::Pointer converterToNode;

    //read all frames from Lightbox
    for( unsigned int n = 0; n < lightbox->getFrames(); n++ )
    {
      lightboxPicDescriptorList.push_back( lightbox->fetchPic(n) );
    }
    switch ( m_UseReader )
    {
      case 0:
      {
        converterToNode = ImageNumberFilter::New();
        break;
      }
      case 1:
      {
        converterToNode = SpacingSetFilter::New();
        break;
      }
      case 2:
      {
        converterToNode = SingleSpacingFilter::New();
        break;
      }
      default:
      {
        std::cout << "ChiliPlugin (LoadImagesFromLightbox): ReaderType undefined." << std::endl;
        return resultVector;
      }
    }

    converterToNode->SetInput( lightboxPicDescriptorList, lightbox->currentSeries()->oid );
    converterToNode->Update();
    resultVector = converterToNode->GetOutput();

    QApplication::restoreOverrideCursor();
  }
  return resultVector;
#endif
}

/** Load all image- and text-files from the series. This function use LoadAllImagesFromSeries(...) and LoadAllTextsFromSeries(...). */
std::vector<mitk::DataTreeNode::Pointer> mitk::ChiliPlugin::LoadCompleteSeries( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  std::vector<DataTreeNode::Pointer> resultNodes;
  resultNodes.clear();

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultNodes;

#else

  if( m_tempDirectory.empty() )
    return resultNodes;

  if( seriesOID == "" )
  {
    QMessageBox::information( 0, "MITK", "No SeriesOID set. Do you select a Series?" );
    return resultNodes;
  }

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  //first: load the image from db
  resultNodes = LoadAllImagesFromSeries( seriesOID );
  //second: load the text-files from db
  std::vector<DataTreeNode::Pointer> tempResultNodes = LoadAllTextsFromSeries( seriesOID );
  while( !tempResultNodes.empty() )
  //add the text-nodes to the image-nodes
  {
    resultNodes.push_back( tempResultNodes.back() );
    tempResultNodes.pop_back();
  }

  QApplication::restoreOverrideCursor();
  return resultNodes;
#endif
}

/** This function load all images from the given series(OID). This function load all files via FileDownload from chili. Chili save the files in the same file-format, like they saved on the server. That mean that *.pic or *.dcm are possible. Dicomfiles get transformed to pic. The slices get combined with the internal set ReaderType. Should other file-formats saved, they get load from harddisk with the DataTreeNodeFactory. */
std::vector<mitk::DataTreeNode::Pointer> mitk::ChiliPlugin::LoadAllImagesFromSeries( const std::string& mitkHideIfNoVersionCode( seriesOID ) )
{
  std::vector<DataTreeNode::Pointer> resultNodes;
  resultNodes.clear();

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultNodes;

#else
  if( m_tempDirectory.empty() )
    return resultNodes;

  if( seriesOID == "" )
  {
    std::cout << "ChiliPlugin (LoadAllImagesFromSeries): No SeriesOID set. Do you select a Series?" << std::endl;
    return resultNodes;
  }

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  //use the imageList to load images from chili
  m_ImageList.clear();
  unknownImageFormatPath.clear();

  //iterate over all images from this series, save them to harddisk, load them and add the picDescriptors to m_ImageList, unknown imagefiles get saved to unknownImageFormatPath
  iterateImages( (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateLoadImage, this );

  if( !m_ImageList.empty() )
  {
    std::list<ipPicDescriptor*> readerInput;
    readerInput.clear();
    for( std::list<ImageListStruct>::iterator imageIter = m_ImageList.begin(); imageIter != m_ImageList.end(); imageIter++ )
    {
      readerInput.push_back( imageIter->Pic );
    }

    //make the readed picDescriptors to multiple Nodes
    PicDescriptorToNode::Pointer converterToNode;

    switch ( m_UseReader )
    {
      case 0:
      {
        converterToNode = ImageNumberFilter::New();
        break;
      }
      case 1:
      {
        converterToNode = SpacingSetFilter::New();
        break;
      }
      case 2:
      {
        converterToNode = SingleSpacingFilter::New();
        break;
      }
      default:
      {
        std::cout << "ChiliPlugin (LoadImagesFromLightbox): ReaderType undefined." << std::endl;
        return resultNodes;
      }
    }
    converterToNode->SetInput( readerInput, seriesOID );
    converterToNode->Update();
    resultNodes = converterToNode->GetOutput();
    //delete the loaded ipPicDescriptors and m_ImageList
    for( std::list<ImageListStruct>::iterator imageIter = m_ImageList.begin(); imageIter != m_ImageList.end(); imageIter++ )
    {
      ipPicFree( imageIter->Pic );
    }
    m_ImageList.clear();
  }

  //read the rest via DataTreeNodeFactory
  if( !unknownImageFormatPath.empty() )
  {
    DataTreeNodeFactory::Pointer factory = DataTreeNodeFactory::New();
    try
    {
      factory->SetFileName( unknownImageFormatPath.front().c_str() );
      factory->Update();
      for( unsigned int i = 0 ; i < factory->GetNumberOfOutputs(); ++i )
      {
        DataTreeNode::Pointer node = factory->GetOutput( i );
        if ( ( node.IsNotNull() ) && ( node->GetData() != NULL )  )
        {
          //the PicDescriptorToNode add the seriesOID and description automatically, the DataTreeNodeFactory not
          series_t series;
          initSeriesStruct( &series );
          //set the seriesOID
          series.oid = strdup( seriesOID.c_str() );

          //Chili fill the rest
          if( pQuerySeries( this, &series, NULL, NULL ) )
          {
            if( series.description == "" )
              node->SetProperty( "name", new StringProperty( "no description" ) );
            else
              node->SetProperty( "name", new StringProperty( series.description ) );
          }
          clearSeriesStruct( &series );
          node->SetProperty( "SeriesOID", new StringProperty( seriesOID ) );
          resultNodes.push_back( node );
        }
      }
      //if there is no Output, something going wrong -> dont delete the files (until the application get closed)
      if( factory->GetNumberOfOutputs() > 0 )
      {
        while( !unknownImageFormatPath.empty() )
        {
          if( remove( unknownImageFormatPath.front().c_str() ) != 0 )
            std::cout << "ChiliPlugin (LoadAllImagesFromSeries): Not able to  delete file: " << unknownImageFormatPath.front().c_str() << std::endl;
          unknownImageFormatPath.pop_front();
        }
      }
    }
    catch ( itk::ExceptionObject & ex )
      itkGenericOutputMacro( << "Exception during file open: " << ex );
  }

  QApplication::restoreOverrideCursor();
  return resultNodes;
#endif
}

/** This function iterate over all images of a series. The single images get saved to harddisk and the filenames to a internal filelist. */
ipBool_t mitk::ChiliPlugin::GlobalIterateLoadImage( int /*rows*/, int mitkHideIfNoVersionCode(row), image_t* mitkHideIfNoVersionCode(image), void* mitkHideIfNoVersionCode(user_data) )
{
#ifdef CHILI_PLUGIN_VERSION_CODE
  ChiliPlugin* callingObject = static_cast<ChiliPlugin*>( user_data );

  //get the FileExtension
  std::string imagePath = image->path;
  std::string fileExtension = imagePath.substr( imagePath.find_last_of(".") + 1 );

  //create a new FileName (1.*, 2.*, 3.*, ...) with FileExtension from DB
  std::ostringstream stringHelper;
  stringHelper << row << "." << fileExtension;
  std::string pathAndFile = callingObject->m_tempDirectory + stringHelper.str();

  //save to harddisk
  ipInt4_t error;
  pFetchDataToFile( image->path, pathAndFile.c_str(), &error );
  //all right?
  if( error != 0 )
    std::cout << "ChiliPlugin (GlobalIterateLoadImage): ChiliError: " << error << ", while reading file (" << image->path << ") from Database." << std::endl;
  else
  {
    if( fileExtension == "pic" )
    {
      ipPicDescriptor *pic;
      pic = ipPicGet( (char*)pathAndFile.c_str(), NULL );
      if( pic != NULL )
      {
        ImageListStruct newImage;
        newImage.Pic = pic;
        newImage.ImageInstanceUID = image->instanceUID;
        callingObject->m_ImageList.push_back( newImage );
        if( remove(  pathAndFile.c_str() ) != 0 )
          std::cout << "ChiliPlugin (GlobalIterateLoadImage): Not able to  delete file: "<< pathAndFile << std::endl;
      }
    }
    else
    {
      if( fileExtension == "dcm" )
      {
        // read dicom-files
        ipUInt1_t* header = NULL;
        ipUInt4_t  header_size;
        ipUInt1_t* dcimage = NULL;
        ipUInt4_t  image_size;

        dicomGetHeaderAndImage( (char*)pathAndFile.c_str(), &header, &header_size, &dcimage, &image_size );

        if( !header )
          std::cout<< "ChiliPlugin (GlobalIterateLoadImage): Could not get header." <<std::endl;

        if( !dcimage )
          std::cout<< "ChiliPlugin (GlobalIterateLoadImage): Could not get image." <<std::endl;

        //ipPicDescriptor *pic = dicomToPic( header, header_size, image, image_size );
        ipPicDescriptor *pic = _dicomToPic( header, header_size, dcimage, image_size, ipFalse, ipTrue, ipTrue);

        if( pic != NULL)
        {
          ImageListStruct newImage;
          newImage.Pic = pic;
          newImage.ImageInstanceUID = image->instanceUID;
          callingObject->m_ImageList.push_back( newImage );
          if( remove(  pathAndFile.c_str() ) != 0 )
            std::cout << "ChiliPlugin (GlobalIterateLoadImage): Not able to  delete file: "<< pathAndFile << std::endl;
        }
      }
      else
      {
        callingObject->unknownImageFormatPath.push_back( pathAndFile );
      }
    }
  }
#endif
  return ipTrue; // enum from ipTypes.h
}

/** This function load all text-files from the series. Chili combine the filename, the OID, MimeType, ... to create the databasedirectory, so different files can be saved with the same filename. The filename from database is used to save the files. So we have to work sequently, otherwise we override the files ( twice filenames ). */
std::vector<mitk::DataTreeNode::Pointer> mitk::ChiliPlugin::LoadAllTextsFromSeries( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
  std::vector<DataTreeNode::Pointer> resultNodes;
  resultNodes.clear();

#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return resultNodes;

#else

  if( m_tempDirectory.empty() )
    return resultNodes;

  if( seriesOID == "" )
  {
    std::cout << "ChiliPlugin (LoadAllTextsFromSeries): No SeriesOID set. Do you select a Series?" << std::endl;
    return resultNodes;
  }

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  //this is the list of all loaded files from chili
  m_TextFileList.clear();

  //now we iterate over all text-files from the series, save the db-path and text-oid to m_TextFileList
  //dont save here to harddisk; double filedescriptions override themselve
  pIterateTexts( this, (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateToLoadAllText, this );

  DataTreeNode::Pointer tempNode;
  //use the found oid's and path's to load
  while( !m_TextFileList.empty() )
  {
    tempNode = LoadOneText( seriesOID, m_TextFileList.front().TextFileOID, m_TextFileList.front().TextFilePath );
    if( tempNode.IsNotNull() )
      resultNodes.push_back( tempNode );
    m_TextFileList.pop_front();
  }

  QApplication::restoreOverrideCursor();
  return resultNodes;
#endif
}

#ifdef CHILI_PLUGIN_VERSION_CODE
/** This function iterate over all text-files from a series and save the text-path and text-oid to the m_TextFileList. */
ipBool_t mitk::ChiliPlugin::GlobalIterateToLoadAllText( int rows, int row, text_t *text, void *user_data )
{
  //cast to chiliplugin to save the single textfileinformation
  ChiliPlugin* callingObject = static_cast<ChiliPlugin*>(user_data);

  //we dont want that to load the parent-child-relation-file
  std::string chiliDatabaseName = text->chiliText;
  std::string fileName = chiliDatabaseName.substr( chiliDatabaseName.find_last_of("-") + 1 );
  if( fileName != "ParentChild.xml" )
  {
    ChiliPlugin::TextFilePathAndOIDStruct newTextFile;
    newTextFile.TextFilePath = text->chiliText;
    newTextFile.TextFileOID = text->oid;
    callingObject->m_TextFileList.push_back( newTextFile );
  }
  return ipTrue; // enum from ipTypes.h
}
#endif

/** To load a single text-file, you need more than the textOID, this function search for the missing attributes and use LoadOneText( const std::string& seriesOID, const std::string& textOID, const std::string& textPath ). */
mitk::DataTreeNode::Pointer mitk::ChiliPlugin::LoadOneText( const std::string& mitkHideIfNoVersionCode(textOID) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return NULL;

#else

  if( m_tempDirectory.empty() )
    return NULL;

  if( textOID == "" )
  {
    std::cout << "ChiliPlugin (LoadOneTextFromSeries): No Text-OID set. Abort." << std::endl;
    return NULL;
  }

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  text_t text;
  series_t series;
  initTextStruct( &text );
  initSeriesStruct( &series );

  text.oid = strdup( textOID.c_str() );
  if( !pQueryText( this, &text, &series, NULL, NULL ) )
  {
    clearTextStruct( &text );
    clearSeriesStruct( &series );
    std::cout << "ChiliPlugin (GetTextInformation): pQueryText() failed. Abort." << std::endl;
    QApplication::restoreOverrideCursor();
    return NULL;
  }

  mitk::DataTreeNode::Pointer result = LoadOneText( series.oid, text.oid, text.chiliText );
  clearTextStruct( &text );
  clearSeriesStruct( &series );
  QApplication::restoreOverrideCursor();
  return result;
#endif
}

/** This function load a single text-file. The file get saved to harddisk, get readed via factory ( current: mitkImageWriterFactory, mitkPointSetWriterFactory, mitkSurfaceVtkWriterFactory ) to mitk and deleted from harddisk. */
mitk::DataTreeNode::Pointer mitk::ChiliPlugin::LoadOneText( const std::string& mitkHideIfNoVersionCode(seriesOID), const std::string& mitkHideIfNoVersionCode(textOID), const std::string& mitkHideIfNoVersionCode(textPath) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return NULL;

#else

  if( m_tempDirectory.empty() )
    return NULL;

  if( seriesOID == "" || textOID == "" ||textPath == "" )
  {
    std::cout << "ChiliPlugin (LoadOneText): Wrong textOID, seriesOID or textPath. Abort." << std::endl;
    return NULL;
  }

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  //get Filename and path to save to harddisk
  std::string fileName = textPath.substr( textPath.find_last_of("-") + 1 );
  std::string pathAndFile = m_tempDirectory + fileName;

  //Load File from DB
  ipInt4_t error;
  pFetchDataToFile( textPath.c_str(), pathAndFile.c_str(), &error );
  if( error != 0 )
  {
    std::cout << "ChiliPlugin (LoadOneText): ChiliError: " << error << ", while reading file (" << fileName << ") from Database." << std::endl;
    QApplication::restoreOverrideCursor();
    return NULL;
  }

  //if there are no fileextension in the filename, the reader not able to load the file and the plugin crashed
  if( fileName.find_last_of(".") == -1 )
  {
    //if the user dont close chili, he can look at the file in the tempdirectory, therefore the file downloaded first and checked afterwards
    std::cout << "ChiliPlugin (LoadOneText): Reader not able to read file without extension.\nIf you dont close Chili you can find the file here: "<< pathAndFile <<"." << std::endl;
  }
  else
  {
    //try to Read the File
    DataTreeNodeFactory::Pointer factory = DataTreeNodeFactory::New();
    try
    {
      factory->SetFileName( pathAndFile );
      factory->Update();
      for ( unsigned int i = 0 ; i < factory->GetNumberOfOutputs( ); ++i )
      {
        DataTreeNode::Pointer node = factory->GetOutput( i );
        if ( ( node.IsNotNull() ) && ( node->GetData() != NULL )  )
        {
          //get the FileExtension and cut them from the filename
          std::string fileNameWithoutExtension = fileName.substr( 0, fileName.find_last_of(".") );
          //set the filename without extension as name-property
          node->SetProperty( "name", new StringProperty( fileNameWithoutExtension ) );
          node->SetProperty( "TextOID", new StringProperty( textOID ) );
          node->SetProperty( "SeriesOID", new StringProperty( seriesOID ) );
          //it should be possible to override all non-image-entries
          node->SetProperty( "CHILI: MANUFACTURER", new StringProperty( "MITK" ) );
          node->SetProperty( "CHILI: INSTITUTION NAME", new StringProperty( "DKFZ.MBI" ) );

          if( remove(  pathAndFile.c_str() ) != 0 )
            std::cout << "ChiliPlugin (LoadOneTextFromSeries): Not able to  delete file: " << pathAndFile << std::endl;

          QApplication::restoreOverrideCursor();
          return node;
        }
      }
    }
    catch ( itk::ExceptionObject & ex )
      itkGenericOutputMacro( << "Exception during file open: " << ex );
  }
  QApplication::restoreOverrideCursor();
  return NULL;
#endif
}

/** This function load the single saved volumes and the relationship between them. Therefore a list get used and filled step by step. */
void mitk::ChiliPlugin::LoadParentChildRelation( const std::string& seriesOID )
{
  if( seriesOID == "" ) return;

  std::vector<mitk::DataTreeNode::Pointer> resultVector;
  resultVector.clear();

  CheckCurrentSeriesForRelation( seriesOID );

  //check
  if( !m_currentXmlDoc || m_ParentOID == "" || m_ParentOID == "stop" ) return;
  TiXmlElement* volume = m_currentXmlDoc->FirstChildElement("volumes");
  TiXmlElement* relation = m_currentXmlDoc->FirstChildElement("relations");
  if( !volume || !relation ) return;

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  //initialize imageList -> contain the imageinstanceuid and the picdescriptor
  m_ImageList.clear();
  iterateImages( (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateLoadImage, this );

  //use the volumelist to combine
  for( TiXmlElement* singleVolume = volume->FirstChildElement(); singleVolume; singleVolume = singleVolume->NextSiblingElement() )
  {
    std::string volumeDescription = singleVolume->Value();  //the columeDescription
    std::list< ipPicDescriptor* > imageInput;  //we need a list if the volume include images
    imageInput.clear();
    for( TiXmlElement* singleID = singleVolume->FirstChildElement(); singleID; singleID = singleID->NextSiblingElement() )
    {
      std::string identification = singleID->Value();
      std::string content = singleID->GetText();

      if( identification == "TextOID" )
      {
        ParentChildStruct newElement;
        newElement.Node = LoadOneText( content );
        newElement.VolumeDescription = volumeDescription;
        newElement.ParentCount = 0;
        newElement.ChildList.clear();
        m_ParentChildList.push_back( newElement );
      }
      else
      {
        if( identification == "ImageInstanceUID" )
        {
          bool picDescriptorFound = false;
          for( std::list<ImageListStruct>::iterator imageListIter =  m_ImageList.begin(); imageListIter != m_ImageList.end(); imageListIter++ )
          {
            if( imageListIter->ImageInstanceUID == content )
            {
              imageInput.push_back( imageListIter->Pic );
              picDescriptorFound = true;
              break;
            }
          }
          if( !picDescriptorFound )
          {
            std::cout << "ChiliPlugin (LoadParentChildRelation): Not able to create Volume " << volumeDescription << "."<< std::endl;
            imageInput.clear();
            break;
          }
        }
      }
    }
    if( !imageInput.empty() )
    {
      PicDescriptorToNode::Pointer converterToNode;
      converterToNode = SingleSpacingFilter::New();
      converterToNode->SetInput( imageInput, seriesOID );
      converterToNode->Update();
      std::vector<mitk::DataTreeNode::Pointer> tempResult = converterToNode->GetOutput();
      ParentChildStruct newElement;
      if( tempResult.size() > 1 )
        std::cout << "ChiliPlugin (LoadParentChildRelation): ERROR: One Volume create more then one Image."<< std::endl;
      newElement.Node = tempResult[0];
      newElement.VolumeDescription = volumeDescription;
      newElement.ParentCount = 0;
      newElement.ChildList.clear();
      m_ParentChildList.push_back( newElement );
    }
  }

  //delete the loaded ipPicDescriptors and m_ImageList
  for( std::list<ImageListStruct>::iterator imageIter = m_ImageList.begin(); imageIter != m_ImageList.end(); imageIter++ )
  {
    ipPicFree( imageIter->Pic );
  }
  m_ImageList.clear();

  //use the saved relations and set the ParentChildList
  AddRelationToParentChildList();

  //add all "parent"-nodes to datastorage
  for( std::list<ParentChildStruct>::iterator listIter = m_ParentChildList.begin(); listIter != m_ParentChildList.end(); listIter++ )
  {
    if( listIter->ParentCount == 0 )
      DataStorage::GetInstance()->Add( listIter->Node );
  }

  //now add all children
  bool allNodesAdded = false;
  while( !allNodesAdded )
  {
    //search for parents
    std::list<ParentChildStruct>::iterator parentLessElement = m_ParentChildList.begin();
    for( ; parentLessElement != m_ParentChildList.end(); parentLessElement++ )
    {
      if( parentLessElement->ParentCount == 0 )
        break;
    }

    if( parentLessElement != m_ParentChildList.end() )
    {
      //mark the used element
      parentLessElement->ParentCount = -1;
      //decrease parent count of all children
      for( std::list<std::string>::iterator childIter = parentLessElement->ChildList.begin(); childIter != parentLessElement->ChildList.end(); childIter++ )
      {
        for( std::list<ParentChildStruct>::iterator childListIter = m_ParentChildList.begin(); childListIter != m_ParentChildList.end(); childListIter++ )
        {
          if( childListIter->VolumeDescription == (*childIter) )
          {
            childListIter->ParentCount--;
            DataStorage::GetInstance()->Add( childListIter->Node, parentLessElement->Node );
            break;
          }
        }
      }
    }
    else
      allNodesAdded = true;
  }

  // stupid, that this is still necessary
  DataTreePreOrderIterator treeiter( app->GetTree() );
  app->GetMultiWidget()->InitializeStandardViews( &treeiter );
  app->GetMultiWidget()->Fit();
  app->GetMultiWidget()->ReInitializeStandardViews();
  RenderingManager::GetInstance()->RequestUpdateAll();
  qApp->processEvents();

  std::string pathAndFile = m_tempDirectory + "ParentChild.xml";
  if( remove(  pathAndFile.c_str() ) != 0 )
    std::cout << "ChiliPlugin (LoadParentChildRelation): Not able to  delete file: "<< pathAndFile << std::endl;

  QApplication::restoreOverrideCursor();
}

/** SAVE-FUNCTIONS */

/** This function provides a dialog where the user can decide if he want to create a new series, save to series, override, ... . Then SaveAsNewSeries(...) or SaveToSeries(...) get used. */
void mitk::ChiliPlugin::SaveToChili( DataStorage::SetOfObjects::ConstPointer mitkHideIfNoVersionCode(inputNodes) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() || inputNodes->begin() == inputNodes->end() )
    return;

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );
  QmitkChiliPluginSaveDialog chiliPluginDialog( 0 );

  //add all nodes to the dialog
  for( DataStorage::SetOfObjects::const_iterator nodeIter = inputNodes->begin(); nodeIter != inputNodes->end(); nodeIter++ )
  {
    if( (*nodeIter) )
    {
      mitk::BaseProperty::Pointer seriesOIDProperty = (*nodeIter)->GetProperty( "SeriesOID" );

      if( seriesOIDProperty.IsNotNull() )
      {
        //this nodes are loaded from chili
        PatientInformation tempPatient = GetPatientInformation( seriesOIDProperty->GetValueAsString() );
        StudyInformation tempStudy = GetStudyInformation( seriesOIDProperty->GetValueAsString() );
        SeriesInformation tempSeries = GetSeriesInformation( seriesOIDProperty->GetValueAsString() );

        chiliPluginDialog.AddStudySeriesAndNode( tempStudy.OID, tempPatient.Name, tempPatient.ID, tempStudy.Description, tempSeries.OID, tempSeries.Number, tempSeries.Description, (*nodeIter) );
      }
      else
      {
        //this nodes saved the first time to chili
        chiliPluginDialog.AddNode( (*nodeIter) );
      }
    }
  }

  //add the current selected study/patient/series
  PatientInformation currentselectedPatient = GetPatientInformation();
  StudyInformation currentselectedStudy = GetStudyInformation();
  SeriesInformation currentselectedSeries = GetSeriesInformation();
  if( currentselectedSeries.OID == "" )
  {
    chiliPluginDialog.AddStudy( currentselectedStudy.OID, currentselectedPatient.Name, currentselectedPatient.ID, currentselectedStudy.Description );
  }
  else
    chiliPluginDialog.AddStudyAndSeries( currentselectedStudy.OID, currentselectedPatient.Name, currentselectedPatient.ID, currentselectedStudy.Description, currentselectedSeries.OID, currentselectedSeries.Number, currentselectedSeries.Description );

  //let the user decide
  chiliPluginDialog.UpdateView();
  QApplication::restoreOverrideCursor();
  int dialogReturnValue = chiliPluginDialog.exec();

  if( dialogReturnValue == QDialog::Rejected )
    return; //user abort

  bool override;
  if( chiliPluginDialog.GetSelection().UserDecision == QmitkChiliPluginSaveDialog::OverrideAll )
    override = true;
  else
    override = false;

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );
  if( chiliPluginDialog.GetSelection().UserDecision == QmitkChiliPluginSaveDialog::CreateNew )
  {
    SaveAsNewSeries( inputNodes, chiliPluginDialog.GetSelection().StudyOID, chiliPluginDialog.GetSeriesInformation().SeriesNumber, chiliPluginDialog.GetSeriesInformation().SeriesDescription, override );
  }
  else
      SaveToSeries( inputNodes, chiliPluginDialog.GetSelection().StudyOID, chiliPluginDialog.GetSelection().SeriesOID, override );

  QApplication::restoreOverrideCursor();
#endif
}

/** This function create a new series and use the function SaveToSeries() . No dialog is used. */
void mitk::ChiliPlugin::SaveAsNewSeries( DataStorage::SetOfObjects::ConstPointer mitkHideIfNoVersionCode(inputNodes), std::string mitkHideIfNoVersionCode(studyOID), int mitkHideIfNoVersionCode(seriesNumber), std::string mitkHideIfNoVersionCode(seriesDescription), bool mitkHideIfNoVersionCode(overrideExistingSeries) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() || inputNodes->begin() == inputNodes->end() )
    return;

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  study_t study;
  initStudyStruct( &study );

  if( studyOID != "" )
  {
    study.oid = strdup( studyOID.c_str() );
    if( pQueryStudy( this, &study, NULL ) )
    {
      //create new series
      series_t* newSeries = ( series_t* )malloc( sizeof( series_t ) );
      initSeriesStruct( newSeries );
      newSeries->description = (char*)seriesDescription.c_str();
      newSeries->number = seriesNumber;
      if( pCreateSeries( this, &study, newSeries ) )
      {
        SaveToSeries( inputNodes, study.oid, newSeries->oid, overrideExistingSeries );
      }
      else
        std::cout << "ChiliPlugin (SaveToChili): Can not create a new Series." << std::endl;
      delete newSeries;
    }
    else
      std::cout << "ChiliPlugin (SaveAsNewSeries): pQueryStudy failed. Abort." << std::endl;
  }
  else
    std::cout << "ChiliPlugin (SaveAsNewSeries): studyOID is empty. Abort." << std::endl;

  clearStudyStruct( &study );
  QApplication::restoreOverrideCursor();
#endif
}

/** This function save the nodes to via FileUpload to chili. */
void mitk::ChiliPlugin::SaveToSeries( DataStorage::SetOfObjects::ConstPointer mitkHideIfNoVersionCode(inputNodes), std::string mitkHideIfNoVersionCode(studyOID), std::string mitkHideIfNoVersionCode(seriesOID), bool mitkHideIfNoVersionCode(overrideExistingSeries) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() || inputNodes->begin() == inputNodes->end() )
    return;

  QApplication::setOverrideCursor( QCursor( Qt::WaitCursor ) );

  study_t study;
  patient_t patient;
  series_t series;
  initStudyStruct( &study );
  initPatientStruct( &patient );
  initSeriesStruct( &series );

  bool abort = false;
  if( seriesOID == "" )
  {
    std::cout << "ChiliPlugin (SaveToSeries): seriesOID is empty. Abort." << std::endl;
    abort = true;
  }
  else
  {
    series.oid = strdup( seriesOID.c_str() );
    if( !pQuerySeries( this, &series, &study, &patient ) )
    {
      std::cout << "ChiliPlugin (SaveToSeries): pQuerySeries failed. Abort." << std::endl;
      abort = true;
    }
    else
      if( study.oid != studyOID )
      {
        std::cout << "ChiliPlugin (SaveToSeries): The given StudyOID is different to the StudyOID of the found Series. Abort." << std::endl;
        abort = true;
      }
  }

  if( abort )
  {
    clearStudyStruct( &study );
    clearPatientStruct( &patient );
    clearSeriesStruct( &series );
    QApplication::restoreOverrideCursor();
    return;
  }

  //check if a file for the parent-child-relationship always exist in this series
  CheckCurrentSeriesForRelation( seriesOID );

  //get current highest imagenumber --> use as input for imgeToPicDescriptor
  int imageNumberCount = GetMaximumImageNumber( seriesOID ) + 1;

  //get the needed study-, patient- and seriesinformation to save
  mitk::ImageToPicDescriptor::TagInformationList picTagList = GetNeededTagList( &study, &patient, &series );

  //start and use every single node
  for( DataStorage::SetOfObjects::const_iterator nodeIter = inputNodes->begin(); nodeIter != inputNodes->end(); nodeIter++ )
  {
    if( (*nodeIter) )
    {
      BaseData* data = (*nodeIter)->GetData();
      if ( data )
      {
        Image* image = dynamic_cast<Image*>( data );
        if( image )
        //save Image
        {
          mitk::BaseProperty::Pointer seriesOIDProperty = (*nodeIter)->GetProperty( "SeriesOID" );

          //ImageToPicDescriptor
          ImageToPicDescriptor::Pointer converterToDescriptor = ImageToPicDescriptor::New();
          //Input
          LevelWindowProperty::Pointer levelWindowProperty = dynamic_cast<LevelWindowProperty*>( (*nodeIter)->GetProperty("levelwindow").GetPointer() );
          if( levelWindowProperty.IsNotNull() )
          {
            converterToDescriptor->SetLevelWindow( levelWindowProperty->GetLevelWindow() );
          }
          converterToDescriptor->SetImage( image );
          converterToDescriptor->SetImageNumber( imageNumberCount );

          //add the node-name as SeriesDescription to the Taglist (get saved as SeriesDescription)
          mitk::BaseProperty::Pointer nameProperty = (*nodeIter)->GetProperty( "name" );
          mitk::ImageToPicDescriptor::TagInformationStruct temp;
          temp.PicTagDescription = tagSERIES_DESCRIPTION;
          temp.PicTagContent = nameProperty->GetValueAsString();
          picTagList.push_back( temp );

          //the second condition is a special case; the user have the oportunity to "save the new nodes only"; but the parent-child-relationship between the "old" and new node should created too; therefore we needed the volumedescription at the relationship and have to use "AddVolumeToParentChild(...)"; therefore we need the original imageinstanceuids; so SetTagList( picTagList, true );
          if( overrideExistingSeries && seriesOIDProperty && seriesOIDProperty->GetValueAsString() == seriesOID || seriesOIDProperty && seriesOIDProperty->GetValueAsString() == seriesOID && !overrideExistingSeries )
            converterToDescriptor->SetTagList( picTagList, true );
          else
            converterToDescriptor->SetTagList( picTagList, false );
          //create the picdescriptorlist
          converterToDescriptor->Update();
          std::list< ipPicDescriptor* > myPicDescriptorList = converterToDescriptor->GetOutput();
          std::list< std::string > newVolume = converterToDescriptor->GetSaveImageInstanceUIDs();
          if( m_ParentOID != "stop" )
            AddVolumeToParentChild( newVolume, (*nodeIter), true );

          //if the user choosen "save the new nodes only" and the current node always saved in the current series, we add the volume to the parent child relationship, but not the slices to chili
          if( seriesOIDProperty && seriesOIDProperty->GetValueAsString() == seriesOID && !overrideExistingSeries )
            continue;

          //check if we have the right to override the data, if the node always saved to series
          if( overrideExistingSeries && seriesOIDProperty && seriesOIDProperty->GetValueAsString() == seriesOID )
          {
            mitk::BaseProperty::Pointer manufacturerProperty = (*nodeIter)->GetProperty( "CHILI: MANUFACTURER" );
            mitk::BaseProperty::Pointer institutionNameProperty = (*nodeIter)->GetProperty( "CHILI: INSTITUTION NAME" );
            if( !manufacturerProperty || !institutionNameProperty || manufacturerProperty->GetValueAsString() != "MITK" || institutionNameProperty->GetValueAsString() != "DKFZ.MBI" )
              continue;
          }

          //increase the imageNumber
          imageNumberCount = imageNumberCount + myPicDescriptorList.size();
          //delete the current node-name from the picTagList (added as SeriesDescription but the picTagList get used for the next image too, that get another SeriesDescription)
          picTagList.pop_back();

          int count = 0;
          while( !myPicDescriptorList.empty() )
          {
            std::ostringstream helpFileName;
            if( (*nodeIter)->GetProperty( "name" )->GetValueAsString() != "" )
              helpFileName << (*nodeIter)->GetProperty( "name" )->GetValueAsString();
            else helpFileName << "FileUpload";
            helpFileName << count << ".pic";
            std::string fileName = helpFileName.str();

            std::string pathAndFile = m_tempDirectory + fileName;
            //get PicDescriptor
            ipPicDescriptor *pic = myPicDescriptorList.front();
            //save to harddisk
            ipPicPut( (char*)pathAndFile.c_str(), pic );
            //delete Descriptor
            ipPicFree( pic );
            myPicDescriptorList.pop_front();
            //save saved file to chili
            if( !pStoreDataFromFile( pathAndFile.c_str(), fileName.c_str(), NULL, NULL, study.instanceUID, patient.oid, study.oid, series.oid, NULL ) )
            {
              std::cout << "ChiliPlugin (SaveToChili): Error while saving File (" << fileName << ") to Database." << std::endl;
            }
            else
            {
              if( remove( pathAndFile.c_str() ) != 0 )
                std::cout << "ChiliPlugin (SaveToChili): Not able to  delete file: " << pathAndFile << std::endl;
            }
            count++;
          }
        }
        else
        //save text
        {
          //DEFINE TEXTOID
          std::string textOID;
          //first: check if the node have a TextOID-Property
          BaseProperty::Pointer propertyTextOID = (*nodeIter)->GetProperty( "TextOID" );
          bool TextExistInCurrentSeries = false;
          if( propertyTextOID )
          {
            //now test if the TextOID existing in the series to save
            text_t text;
            series_t series;
            initTextStruct( &text );
            initSeriesStruct( &series );

            text.oid = strdup( propertyTextOID->GetValueAsString().c_str() );
            //get the series to the given TextOID
            if( pQueryText( this, &text, &series, NULL, NULL ) )
            {
              //the series are the same -> the text always exist in the current series
              if( strcmp( series.oid, seriesOID.c_str() ) == 0 )
              {
                TextExistInCurrentSeries = true;
              }
            }
            clearTextStruct( &text );
            clearSeriesStruct( &series );
          }

          bool textShouldNotBeOverride = false;
          //second: handle if text exist in series and text should override
          if( !TextExistInCurrentSeries )
            textOID = dbGetNewOID();  //they dont exist, so we need a new OID
          else
          {
            textOID = propertyTextOID->GetValueAsString();  //to override the oid have to be the same
            if( !overrideExistingSeries )
              textShouldNotBeOverride = true;
          }

          //add volume to parent-child ( the function check if volume always exist )
          if( m_ParentOID != "stop" )
          {
            //AddVolumeToParentChild want a std::list
            std::list< std::string > newVolume;
            newVolume.clear();
            newVolume.push_back( textOID );
            AddVolumeToParentChild( newVolume, (*nodeIter), false );
          }

          if( textShouldNotBeOverride ) continue;  //we dont want to override existing text, so we dont need to save!

          //SAVE
          //first: Searching for possible Writer
          std::list<FileWriter::Pointer> possibleWriter;
          std::list<LightObject::Pointer> allObjects = itk::ObjectFactoryBase::CreateAllInstance( "IOWriter" );
          for( std::list<LightObject::Pointer>::iterator iter = allObjects.begin(); iter != allObjects.end(); iter++ )
          {
            FileWriter* writer = dynamic_cast<FileWriter*>( iter->GetPointer() );
            if( writer )
              possibleWriter.push_back( writer );
            else std::cout << "ChiliPlugin (SaveToChili): no FileWriter returned" << std::endl;
          }

          //second: use Writer
          for( std::list<FileWriter::Pointer>::iterator it = possibleWriter.begin(); it != possibleWriter.end(); it++ )
          {
            if( it->GetPointer()->CanWrite( (*nodeIter) ) )
            {
              //create filename
              std::string fileName;
              if( (*nodeIter)->GetProperty( "name" )->GetValueAsString() != "" )
                fileName = (*nodeIter)->GetProperty( "name" )->GetValueAsString();
              else fileName = "FileUpload";
              //set extension
              fileName = fileName + it->GetPointer()->GetFileExtension();
              //path and filename
              std::string pathAndFile = m_tempDirectory + fileName;

              //save via Writer to harddisk
              it->GetPointer()->SetFileName( pathAndFile.c_str() );
              it->GetPointer()->SetInput( (*nodeIter) );
              it->GetPointer()->Write();

              //save file from harddisk to chili
              if( !pStoreDataFromFile( pathAndFile.c_str(), fileName.c_str(), it->GetPointer()->GetWritenMIMEType().c_str(), NULL, study.instanceUID, patient.oid, study.oid, series.oid, textOID.c_str() ) )
              {
                std::cout << "ChiliPlugin (SaveToChili): Error while saving File (" << fileName << ") to Database." << std::endl;
              }
              else
              {
                //delete saved file
                if( remove(  pathAndFile.c_str() ) != 0 )
                  std::cout << "ChiliPlugin (SaveToChili): Not able to  delete file: " << it->GetPointer()->GetFileName() << std::endl;
              }
            }
          }
        }
      }
    }
  }
  //save relationship
  if( m_ParentOID != "stop" )
  {
    SaveRelationShip();
    //add the saved file to the series
    std::string pathAndFile = m_tempDirectory + "ParentChild.xml";
    if( !pStoreDataFromFile( pathAndFile.c_str(), "ParentChild.xml", "application/MITK.xml", NULL, study.instanceUID, patient.oid, study.oid, series.oid, m_ParentOID.c_str() ) )
    {
      std::cout << "ChiliPlugin (SaveToChili): Error while saving parent-child-relationship." << std::endl;
    }
    else
      if( remove(  pathAndFile.c_str() ) != 0 )
        std::cout << "ChiliPlugin (SaveToChili): Not able to  delete file: "<< pathAndFile << std::endl;
  }

  clearStudyStruct( &study );
  clearPatientStruct( &patient );
  clearSeriesStruct( &series );
  QApplication::restoreOverrideCursor();
#endif
}

/** RELATIONSHIP-FUNCTIONS */

/** Check the series if a ParentChild-TextFile exist and set m_currentXmlDoc. */
void mitk::ChiliPlugin::CheckCurrentSeriesForRelation( const std::string& mitkHideIfNoVersionCode(seriesOID) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() || seriesOID == "" )
    return;

  m_ParentOID = "";
  pIterateTexts( this, (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateTextForRelation, this );
  if( m_ParentOID != "" )
  {
    //load file from chili
    LoadOneText( m_ParentOID );
    //load file from harddisk
    std::string pathAndFile = m_tempDirectory + "ParentChild.xml";
    m_currentXmlDoc = new TiXmlDocument( pathAndFile.c_str() );
    if( !m_currentXmlDoc->LoadFile() )
    {
      std::cout << "ChiliPlugin (CheckCurrentSeriesForRelation): File for parent-child-relationship exist, but not able to load. Abort." << std::endl;
      m_ParentOID = "stop";
    }
  }
  else
  {
    //create new file
    m_currentXmlDoc = new TiXmlDocument();
    TiXmlDeclaration * decl = new TiXmlDeclaration( "1.0", "", "" );
    m_currentXmlDoc->LinkEndChild( decl );
    TiXmlElement * volumes = new TiXmlElement( "volumes" );
    m_currentXmlDoc->LinkEndChild( volumes );
    TiXmlElement * relations = new TiXmlElement( "relations" );
    m_currentXmlDoc->LinkEndChild( relations );
  }

  m_ParentChildList.clear();

#endif
}

#ifdef CHILI_PLUGIN_VERSION_CODE
/** Iterate over all text and search for "ParentChild.xml", the function GetTextInformationList() dont return this one. */
ipBool_t mitk::ChiliPlugin::GlobalIterateTextForRelation( int rows, int row, text_t *text, void *user_data )
{
  //cast to chiliplugin to save the single textfileinformation
  ChiliPlugin* callingObject = static_cast<ChiliPlugin*>(user_data);

  if( text != NULL )
  {
    std::string chiliDataBaseName = text->chiliText;
    std::string fileName = chiliDataBaseName.substr( chiliDataBaseName.find_last_of("-") + 1 );
    if( fileName == "ParentChild.xml" )
    {
      callingObject->m_ParentOID = text->oid;
      return ipFalse;
    }
  }
  return ipTrue;
}
#endif

/** This function add a volume to the xml-file, therefore it check the included one and add only new one. */
void mitk::ChiliPlugin::AddVolumeToParentChild( std::list< std::string > mitkHideIfNoVersionCode(newVolume), DataTreeNode::Pointer mitkHideIfNoVersionCode(node), bool mitkHideIfNoVersionCode(image) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() )
    return;

  //check
  if( !m_currentXmlDoc ) return;
  TiXmlElement* volume = m_currentXmlDoc->FirstChildElement("volumes");
  if( !volume )  return;

  bool createNewVolume = true; //have we to create a new Volume?
  std::string existingVolumeDescription; //get the volumeDescription, if the uids or oids alway exist
  int volumeCount = 0; //count the included volume

  TiXmlElement* singleVolume = volume->FirstChildElement();
  while( singleVolume && createNewVolume )
  {
    volumeCount++;
    //check if the ids from the newVolume alway exist in the saved volume
    TiXmlElement* singleID = singleVolume->FirstChildElement();
    int idCount = 0;  //we have to count the ids, they can be different sized
    bool match = true;
    while( singleID && match )
    {
      //therefore check if the id of the current selected volume exist in the newVolume
      if( find( newVolume.begin(), newVolume.end(), singleID->GetText() ) != newVolume.end() )
      {
        //if id exist, check the next id if they exist in the newVolume
        singleID = singleID->NextSiblingElement();
        idCount++;
      }
      else
        match = false;
    }
    //do we found a match?
    if( match && idCount == newVolume.size() )
    {
      //set the volumeDescription and dont create a new volume
      createNewVolume = false;
      existingVolumeDescription = singleVolume->Value();
    }
    singleVolume = singleVolume->NextSiblingElement();
  }

  ParentChildStruct newElement;

  if( createNewVolume )
  {
    //create the new volumeDescription
    std::ostringstream stringHelper;
    stringHelper << "volume" << volumeCount + 1;
    std::string elementCount = stringHelper.str();
    TiXmlElement * element = new TiXmlElement( elementCount.c_str() );
    //add the description
    newElement.VolumeDescription = elementCount;
    //add the UIDs
    for( std::list< std::string >::iterator iter = newVolume.begin(); iter != newVolume.end(); iter++ )
    {
      TiXmlElement * uid;
      if( image ) uid = new TiXmlElement( "ImageInstanceUID" );
      else uid = new TiXmlElement( "TextOID" );
      TiXmlText * text = new TiXmlText( (*iter).c_str() );
      uid->LinkEndChild( text );
      element->LinkEndChild( uid );
    }
    volume->LinkEndChild( element );
    std::string pathAndFile = m_tempDirectory + "ParentChild.xml";
    m_currentXmlDoc->SaveFile( pathAndFile.c_str() );
  }
  else
    newElement.VolumeDescription = existingVolumeDescription;

  newElement.Node = node;
  newElement.ParentCount = 0;
  newElement.ChildList.clear();
  m_ParentChildList.push_back( newElement );

#endif
}

/** This function save the relations between the nodes. */
void mitk::ChiliPlugin::SaveRelationShip()
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  if( m_tempDirectory.empty() )
    return;

  //check
  if( !m_currentXmlDoc ) return;
  TiXmlElement* relation = m_currentXmlDoc->FirstChildElement("relations");
  if( !relation ) return;

  //first delete all existing relationships between the overgiven nodes, we create new one
  DeleteExistingRelations();

  //we need a structur to test if circles created
  AddRelationToParentChildList();

  //add the new relations
  for( std::list<ParentChildStruct>::iterator listIter = m_ParentChildList.begin(); listIter != m_ParentChildList.end(); listIter++ )
  {
    //get the relation from DataStorage
    DataStorage::SetOfObjects::ConstPointer relations = DataStorage::GetInstance()->GetSources( listIter->Node );
    for( DataStorage::SetOfObjects::const_iterator relationIter = relations->begin(); relationIter != relations->end(); relationIter++ )
    {
      //searching for the "parent"-node
      std::list<ParentChildStruct>::iterator getParent = m_ParentChildList.begin();
      for( getParent; getParent != m_ParentChildList.end(); getParent++ )
      {
        if( getParent->Node == (*relationIter) )
          break;
      }

      if( getParent->Node != (*relationIter) )
        continue;  //parent not stored
      else
      {  //parent stored
        if( !RelationCreateCircle( getParent->VolumeDescription, listIter->VolumeDescription ) )
        {
          std::string relationship = getParent->VolumeDescription + " " + listIter->VolumeDescription;
          TiXmlElement * relationElement = new TiXmlElement( "Relation" );
          TiXmlText * text = new TiXmlText( relationship.c_str() );
          relationElement->LinkEndChild( text );
          relation->LinkEndChild( relationElement );
        }
        else
          std::cout << "ChiliPlugin (SaveRelationShip): Relation " << getParent->VolumeDescription << " - " << listIter->VolumeDescription << " create a circle. Relation dont add." << std::endl;
      }
    }
  }
  //all changes done, save the xml-file
  std::string pathAndFile = m_tempDirectory + "ParentChild.xml";
  m_currentXmlDoc->SaveFile( pathAndFile.c_str() );
#endif
}

/** This function return if the overgiven values creates circles or not. */
bool mitk::ChiliPlugin::RelationCreateCircle( std::string mitkHideIfNoVersionCode(parent), std::string mitkHideIfNoVersionCode(child) )
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return false;

#else

  //use a copy of m_CircleTestList to add the new relation and test if circles created
  std::list<ParentChildStruct> copyToWork = m_ParentChildList;
  //add relation to workingCopy
  for( std::list<ParentChildStruct>::iterator iter = copyToWork.begin(); iter != copyToWork.end(); iter++ )
  {
    if( iter->VolumeDescription == parent )
      iter->ChildList.push_back( child );
    if( iter->VolumeDescription == child )
      iter->ParentCount++;
  }

  //test circle
  bool search = true;
  bool circleFound = false;
  while( search )
  {
    //search for element with no parent
    bool elementFound = false;
    std::list<ParentChildStruct>::iterator parentLessElement = copyToWork.begin();
    while( parentLessElement != copyToWork.end() && !elementFound )
    {
      if( parentLessElement->ParentCount == 0 ) elementFound = true;
      else parentLessElement++;
    }

    if( elementFound )
    {
      //mark the used element
      parentLessElement->ParentCount = -1;
      //decrease parent count of all children
      for( std::list<std::string>::iterator childListIter = parentLessElement->ChildList.begin(); childListIter != parentLessElement->ChildList.end(); childListIter++ )
      {
        for( std::list<ParentChildStruct>::iterator circleListIter = copyToWork.begin(); circleListIter != copyToWork.end(); circleListIter++ )
        {
          if( circleListIter->VolumeDescription == (*childListIter) )
          {
            circleListIter->ParentCount--;
            break;
          }
        }
      }
    }
    else
    {
      search = false;
      //check if the ParentCount of all elements are -1
      for( std::list<ParentChildStruct>::iterator circleListIter = copyToWork.begin(); circleListIter != copyToWork.end(); circleListIter++ )
      {
        if( circleListIter->ParentCount != -1 )
        {
          circleFound = true;
          break;
        }
      }
    }
  }
  if( !circleFound )
    m_ParentChildList = copyToWork;

  return circleFound;
#endif
}

/** This function use the saved relations ( ParentChild.xml ) and set the attributes "ParentCount" and "ChildList" of m_ParentChildList. */
void mitk::ChiliPlugin::AddRelationToParentChildList()
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  //check
  if( !m_currentXmlDoc ) return;
  TiXmlElement* volume = m_currentXmlDoc->FirstChildElement("volumes");
  TiXmlElement* relation = m_currentXmlDoc->FirstChildElement("relations");
  if( !volume || !relation ) return;

  //now set the count and parentList
  for( TiXmlElement* singleRelation = relation->FirstChildElement(); singleRelation; singleRelation = singleRelation->NextSiblingElement() )
  {
    std::string relationString = singleRelation->GetText();
    std::string parent = relationString.substr( 0, relationString.find(" ") );
    std::string child = relationString.substr( relationString.find(" ") + 1 );

    for( std::list<ParentChildStruct>::iterator iter = m_ParentChildList.begin(); iter != m_ParentChildList.end(); iter++ )
    {
      if( iter->VolumeDescription == parent )
        iter->ChildList.push_back( child );
      if( iter->VolumeDescription == child )
        iter->ParentCount++;
    }
  }
#endif
}

/** This function use the m_ParentChildList and combine all nodes to check, if a relation between them always saved. If a relation always exist, they get deleted. */
void mitk::ChiliPlugin::DeleteExistingRelations()
{
#ifndef CHILI_PLUGIN_VERSION_CODE

  QMessageBox::information( 0, "MITK", "Sorry, youre current CHILI version does not support this function." );
  return;

#else

  //check
  if( !m_currentXmlDoc ) return;
  TiXmlElement* relation = m_currentXmlDoc->FirstChildElement("relations");
  if( !relation ) return;

  //therefore create a list with all possible combinations between the nodes to add
  std::list<std::string> combinationList;
  combinationList.clear();

  std::list<ParentChildStruct>::iterator listEnd = m_ParentChildList.end();
  listEnd--;
  for( std::list<ParentChildStruct>::iterator listIter = m_ParentChildList.begin(); listIter != listEnd; listIter++ )
  {
    for( std::list<ParentChildStruct>::iterator walkIter = listIter; walkIter != m_ParentChildList.end(); walkIter++ )
    {
      //create combination and save them
      std::string combination = listIter->VolumeDescription + " " + walkIter->VolumeDescription;
      combinationList.push_back( combination );
      combination = walkIter->VolumeDescription + " " + listIter->VolumeDescription;
      combinationList.push_back( combination );
    }
  }

  //now searching for the combination in the xml-file and delete them if they exist
  while( !combinationList.empty() )
  {
    for( TiXmlElement* singleRelation = relation->FirstChildElement(); singleRelation; singleRelation = singleRelation->NextSiblingElement() )
    {
      if( singleRelation->GetText() == combinationList.front() )
      {
        relation->RemoveChild( singleRelation );
        break;
      }
    }
    combinationList.pop_front();
  }
#endif
}

/** This function found the highes imagenumber in a series, its needed because the slice-sort-reader use the imagenumber as criteria. */
int mitk::ChiliPlugin::GetMaximumImageNumber( std::string seriesOID )
{
  m_MaximumImageNumber = 0;
  iterateImages( (char*)seriesOID.c_str(), NULL, &ChiliPlugin::GlobalIterateImagesForMaximalImageNumber, this );
  return m_MaximumImageNumber;
}

/** Function to iterate over all images from a series. */
ipBool_t mitk::ChiliPlugin::GlobalIterateImagesForMaximalImageNumber( int /*rows*/, int /*row*/, image_t* image, void* user_data )
{
  ChiliPlugin* callingObject = static_cast<ChiliPlugin*>( user_data );
  if( image->number > callingObject->m_MaximumImageNumber )
    callingObject->m_MaximumImageNumber = image->number;
  return ipTrue; // enum from ipTypes.h
}

/** Create the needed tags to save a pic-file to chili. */
mitk::ImageToPicDescriptor::TagInformationList mitk::ChiliPlugin::GetNeededTagList( study_t* mitkHideIfNoVersionCode(study), patient_t* mitkHideIfNoVersionCode(patient), series_t* mitkHideIfNoVersionCode(series) )
{
  mitk::ImageToPicDescriptor::TagInformationList resultList;
  resultList.clear();

#ifdef CHILI_PLUGIN_VERSION_CODE

  //get all study- and patient-information
  if( study != NULL && patient != NULL && series != NULL )
  {
    mitk::ImageToPicDescriptor::TagInformationStruct temp;
    //study
    temp.PicTagDescription = tagSTUDY_INSTANCE_UID;
    temp.PicTagContent = study->instanceUID;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSTUDY_DESCRIPTION;
    temp.PicTagContent = study->description;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSTUDY_DATE;
    temp.PicTagContent = study->date;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSTUDY_TIME;
    temp.PicTagContent = study->time;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSTUDY_ID;
    temp.PicTagContent = study->id;
    resultList.push_back( temp );
    //self created
    temp.PicTagDescription = tagMANUFACTURER;
    temp.PicTagContent = "MITK";
    resultList.push_back( temp );
    temp.PicTagDescription = tagINSTITUTION_NAME;
    temp.PicTagContent = "DKFZ.MBI";
    resultList.push_back( temp );
    temp.PicTagDescription = tagMODALITY;
    std::string tempString = study->modality;
    tempString = tempString + " processed";
    temp.PicTagContent = tempString;
    resultList.push_back( temp );
    //series
    temp.PicTagDescription = tagSERIES_INSTANCE_UID;
    temp.PicTagContent = series->instanceUID;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSERIES_DATE;
    temp.PicTagContent = series->date;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSERIES_TIME;
    temp.PicTagContent = series->time;
    resultList.push_back( temp );
    temp.PicTagDescription = tagSERIES_NUMBER;
    temp.PicTagContent = series->number;
    resultList.push_back( temp );
  }
  else
  {
    std::cout << "ChiliPlugin(GetAllAvailablePicTags): One input was empty." << std::endl;
    resultList.clear();
  }
#endif

  return resultList;
}

/** Event if a new study get selected (from QcPlugin). */
void mitk::ChiliPlugin::studySelected( study_t* /*study*/ )
{
  SendStudySelectedEvent();
}

/** Event if the lightbox get tiles (from QcPlugin). */
void mitk::ChiliPlugin::lightboxTiles( QcLightboxManager* /*lbm*/, int tiles )
{
  m_LightBoxCount = tiles;

  for( int row = 0; row < tiles; ++row )
  {
    try
    {
      QIDToolButton* button = m_LightBoxImportButtons.at(row);
      button->show();
    }
    catch(...) {}
  }
  for( int row = tiles; row < NUMBER_OF_THINKABLE_LIGHTBOXES; ++row )
  {
    try
    {
      QIDToolButton* button = m_LightBoxImportButtons.at(row);
      button->hide();
    }
    catch(...) {}
  }

  SendLightBoxCountChangedEvent(); // tell the application
}

/** Button to import the lightbox. */
void mitk::ChiliPlugin::lightBoxImportButtonClicked(int row)
{
  if( m_InImporting ) return;

  if( ChiliIsFillingLightbox() )
  {
    QMessageBox::information( 0, "MITK", "Lightbox not ready. Try again when lightbox filling is completed!" );
    return;
  }

  QPtrList<QcLightbox>& lightboxes = QcPlugin::lightboxManager()->getLightboxes();
  QcLightbox* selectedLightbox = lightboxes.at(row);

  if( selectedLightbox )
  {
    m_InImporting = true;
    std::vector<DataTreeNode::Pointer> resultNodes;
    //dont use "LoadCompleteSeries( ... )", the Buttons called LightboxImportButtons, so use the LoadImagesFromLightbox-Function too
    //for images
    resultNodes = LoadImagesFromLightbox( selectedLightbox );

    if( resultNodes.size() > 8 )
    {
      if( QMessageBox::question( 0, tr("MITK"), QString("MITK detected more then 8 Result-Images.\nMore Images makes the Application slower,\nso you can choose if you want to add the 2D-Images too.\n\nDo you want to add all 2D-Images to the MITK::DataTree?"), QMessageBox::Yes, QMessageBox::No ) == QMessageBox::Yes )
      {
        for( unsigned int n = 0; n < resultNodes.size(); n++ )
          DataStorage::GetInstance()->Add( resultNodes[n] );
      }
      else
      {
        for( unsigned int n = 0; n < resultNodes.size(); n++ )
        {
           mitk::BaseProperty::Pointer propertyImageNumber = resultNodes[n]->GetProperty( "NumberOfSlices" );
          if( propertyImageNumber.IsNull() || ( propertyImageNumber.IsNotNull() && propertyImageNumber->GetValueAsString() != "1" ) )
            DataStorage::GetInstance()->Add( resultNodes[n] );
        }
      }
    }
    else
      for( unsigned int n = 0; n < resultNodes.size(); n++ )
        DataStorage::GetInstance()->Add( resultNodes[n] );

#ifdef CHILI_PLUGIN_VERSION_CODE
    //for text
    if( selectedLightbox->currentSeries() )
    {
      resultNodes = LoadAllTextsFromSeries( selectedLightbox->currentSeries()->oid );
      for( unsigned int n = 0; n < resultNodes.size(); n++ )
        DataStorage::GetInstance()->Add( resultNodes[n] );
    }
#endif

    // stupid, that this is still necessary
    DataTreePreOrderIterator treeiter( app->GetTree() );
    app->GetMultiWidget()->InitializeStandardViews( &treeiter );
    app->GetMultiWidget()->Fit();
    app->GetMultiWidget()->ReInitializeStandardViews();
    RenderingManager::GetInstance()->RequestUpdateAll();
    qApp->processEvents();
    m_InImporting = false;
  }
}

/** Throw an event, if the study changed. */
void mitk::ChiliPlugin::SendStudySelectedEvent()
{
  //throw ITK event (defined in mitkChiliPluginEvents.h)
  InvokeEvent( PluginStudySelected() );
}

/** Throw an event, if the lightboxcount changed. */
void mitk::ChiliPlugin::SendLightBoxCountChangedEvent()
{
  //throw ITK event (defined in mitkChiliPluginEvents.h)
  InvokeEvent( PluginLightBoxCountChanged() );
}

/** Reinitialize the application. */
void mitk::ChiliPlugin::OnApproachReinitializationOfWholeApplication()
{
  static int clickCount = 0;

  clickCount++;

  if (clickCount > 0)
  {
    if ( QMessageBox::question( NULL, tr("MITK"), 
                                  /*QString("Desparate clicking suggests a strong wish to restart the whole MITK plugin.\n\nDo you want to restart MITK?"),*/
                                  QString("Do you want to restart MITK and loose all work results?"),
                                  QMessageBox::Yes | QMessageBox::Default, 
                                  QMessageBox::No  | QMessageBox::Escape
                                ) == QMessageBox::Yes )
    {
      CreateSampleApp();
    }

    clickCount = 0;
  }
}

/** TELECONFERENCE METHODS */

void mitk::ChiliPlugin::connectPartner()
{
  ConferenceToken* ct = ConferenceToken::GetInstance();
  ct->ArrangeToken();
}

void mitk::ChiliPlugin::disconnectPartner()
{
  ConferenceToken* ct = ConferenceToken::GetInstance();
  ct->SetToken( 0 );
}

void mitk::ChiliPlugin::handleMessage( ipInt4_t type, ipMsgParaList_t *list )
{
  //QString message;
  //QString arguments;

  ipInt4_t eventID;
  ipFloat4_t w1,w2,w3,p1,p2;
  ipInt4_t sender,receiver,etype,estate,ebuttonstate,ekey;
  char *str = NULL;

  switch( type )
  {
    case m_QmitkChiliPluginConferenceID + MITKc:
      ipMsgListToVar( list,
                      ipTypeInt4, &eventID,
                      ipTypeStringPtr, &str,
                      ipTypeInt4, &etype, 
                      ipTypeInt4, &estate, 
                      ipTypeInt4, &ebuttonstate, 
                      ipTypeInt4, &ekey, 
                      ipTypeFloat4, &w1,
                      ipTypeFloat4, &w2,
                      ipTypeFloat4, &w3,
                      ipTypeFloat4, &p1,
                      ipTypeFloat4, &p2);

      if(!ConferenceEventMapper::MapEvent(eventID, str,etype,estate,ebuttonstate,ekey,w1,w2,w3,p1,p2))
        //std::cout<<"EventMaper no funciona"<<std::endl;

    break;
    case m_QmitkChiliPluginConferenceID +QTc :
      ipMsgListToVar( list,
                      ipTypeStringPtr, &str );
      //std::cout<<"CONFERENCE: (1) "<<str;
      if( ! EventCollector::PostEvent(QString(str), task))
      //std::cout<<" NO SUCCES!"<<std::endl;
      //std::cout<<std::endl;

    break;
    case m_QmitkChiliPluginConferenceID + ARRANGEc:
        {
        ipMsgListToVar( list,
                        ipTypeInt4, &sender );
        //std::cout<<"CONFERENCE TOKEN ID:  "<<sender<<std::endl;

        ConferenceToken* ct = ConferenceToken::GetInstance();
        ct->ArrangeToken(sender);
        }
    break;
    case m_QmitkChiliPluginConferenceID + TOKENREQUESTc:
        //FROM: AskForToken(...)
        {
        ipMsgListToVar( list,
                        ipTypeInt4, &receiver );
        //std::cout<<"CONFERENCE TOKEN REQUEST FROM:  "<<receiver<<std::endl;

        ConferenceToken* ct = ConferenceToken::GetInstance();
        ct->GetToken(receiver);
        }
    break;
    case m_QmitkChiliPluginConferenceID + TOKENSETc:
        //FROM: AskForToken(...)
        {
        ipMsgListToVar( list,
                        ipTypeInt4, &sender,
                        ipTypeInt4, &receiver );
        //std::cout<<"CONFERENCE TRY SET TOKEN FROM:  "<<sender<<" TO:"<<receiver<<std::endl;

        ConferenceToken* ct = ConferenceToken::GetInstance();
        ct->SetToken( receiver );
        }
    break;
    case m_QmitkChiliPluginConferenceID + MOUSEMOVEc:
      ipMsgListToVar( list,
                      ipTypeStringPtr, &str,
                      ipTypeFloat4, &w1,
                      ipTypeFloat4, &w2,
                      ipTypeFloat4, &w3
          );

      if( !ConferenceEventMapper::MapEvent( str, w1, w2, w3 ) )
        //std::cout<<"ChiliPlugin::handleMessage() -> MouseMoveEvent couldn't pass through"<<std::endl;

    break;   
    default:
//       message = QString( " type %1" ).arg( type );
//       arguments = "argumente unbekannt";
      break;
  }
}

#ifdef WIN32

#include <Windows.h>
#include <stdio.h>
#define BUFSIZE 512

/** Create a temporary directory for windows. */
std::string mitk::ChiliPlugin::GetTempDirectory()
{
/*
  DWORD BufSize=BUFSIZE;
  char PathBuffer[BUFSIZE];
  char TempName[BUFSIZE];  
  DWORD RetVal;
  UINT uRetVal;

  // Get the temp path.
  RetVal = GetTempPath( BufSize, PathBuffer );
  if( RetVal > BufSize || RetVal == 0 )
  {
    return "";
  }

  // Create a unique name.
  uRetVal = GetTempFileName( PathBuffer, "NEW", 0, TempName );
  if( uRetVal == 0 )
  {
    return "";
  }

  std::string makeTmpDirectory = TempName;
  makeTmpDirectory = "mkdir " + makeTmpDirectory;
  system( makeTmpDirectory.c_str() );

  return TempName;
*/

  std::string tmpDirectory = "C:\\WINDOWS\\Temp\\ChiliTemp\\";
  std::string removeDirectory = "rm /s " + tmpDirectory;
  std::string makeDirectory = "mkdir " + tmpDirectory;
  system( removeDirectory.c_str() );
  system( makeDirectory.c_str() );
  return tmpDirectory;
}

#else

#include <stdlib.h>

/** Create a temporary directory for linux. */
std::string mitk::ChiliPlugin::GetTempDirectory()
{
  char *tmpDirectory = getenv( "TMPDIR" ); //return NULL if the TempDirectory is "/tmp/"
  std::string newTmpDirectory;
  if( tmpDirectory == NULL )
    newTmpDirectory = "/tmp/ChiliTempXXXXXX"; //mkdtemp need 6-10 dummies
  else
  {
    newTmpDirectory = tmpDirectory;
    newTmpDirectory = newTmpDirectory + "/ChiliTempXXXXXX";
  }
  tmpDirectory = mkdtemp( (char*)newTmpDirectory.c_str() ); //create a new unique directory
  if( tmpDirectory != NULL )
  {
    newTmpDirectory = tmpDirectory;
    newTmpDirectory = newTmpDirectory + "/";
    return newTmpDirectory;
  }
  else
    return "";
}


#endif

QObject* mitk::ChiliPlugin::findProgressBar(QObject* object)
{
  const QObjectList* children = object->children();
  if (children)
  {
    QObjectListIt it( *children );
    QObject* child;
    while ( (child = it.current()) != 0 )
    {
      //std::cout << "Testing child '" << child->name() << "' (" << child->className() << ")" << std::endl;
      if ( std::string(child->className()) == "QProgressBar" )
        return child;

      QObject* result = findProgressBar( child );
      if (result) 
        return result;

      ++it;
    }
  }

  return NULL;
}

bool mitk::ChiliPlugin::ChiliIsFillingLightbox()
{
  // find lightBoxArea
  QObject* current(this);
  QObject* lightboxAreaObject(NULL);
  while (current)
  {
    /*
    std::cout << "============================================" << std::endl;
    std::cout << "Current object: '" << current->name() << "' (" << current->className() << ")" << std::endl;
    */

    const QObjectList* children = current->children();
    if (children)
    {
      QObjectListIt it( *children );
      QObject* child;
      while ( (child = it.current()) != 0 )
      {
        //std::cout << "  Child '" << child->name() << "' (" << child->className() << ")" << std::endl;

        if ( std::string(child->name()) == "lightboxArea" ) 
        {
          lightboxAreaObject = child;
          break;
        }
        ++it;
      }
    }

    if (lightboxAreaObject) break;

    current = current->parent();
  }

  if (lightboxAreaObject)
  {
    QProgressBar* progressBar = dynamic_cast<QProgressBar*>( findProgressBar( lightboxAreaObject ) );
    if (progressBar)
    {
      //std::cout << "Found progressbar, progress " << progressBar->progress() << std::endl;
      //return progressBar->progress() != 0;
      return progressBar->isVisible();
    }
    else
    {
      //std::cout << "Couldn't find progressbar -- assuming CHILI is not filling lightbox." << std::endl;
      return false;
    }
  }

  return false;
}
    
