/** PageList
 */
#include "pagelist.h"

using namespace ttx;

PageList::PageList(Configure *configure) :
	_configure(configure),
	_iterMag(0),
	_iterSubpage(nullptr)
{
  for (int i=0;i<8;i++)
      _mag[i]=nullptr;
	if (_configure==nullptr)
	{
		std::cerr << "NULL configuration object" << std::endl;
		return;
	}
	LoadPageList(_configure->GetPageDirectory());
}

PageList::~PageList()
{
	// std::cerr << "[PageList] Destructor" << std::endl;
}

int PageList::LoadPageList(std::string filepath)
{
	// std::cerr << "[PageList::LoadPageList] Loading pages from " << filepath << std::endl;
	// Open the directory
	DIR *dp;
	TTXPageStream* q;
	struct dirent *dirp;
	if ((dp = opendir(filepath.c_str())) == NULL)
  {
		std::cerr << "Error(" << errno << ") opening " << filepath << std::endl;
		return errno;
	}
	// Load the filenames into a list
	while ((dirp = readdir(dp)) != NULL)
	{
		//p=new TTXPageStream(filepath+"/"+dirp->d_name);
    if (std::string(dirp->d_name).find(".tti") != std::string::npos)	// Is the file type .tti or ttix?
    {
      q=new TTXPageStream(filepath+"/"+dirp->d_name);
      // If the page loaded, then push it into the appropriate magazine
      if (q->Loaded())
      {
					q->GetPageCount(); // Use for the side effect of renumbering the subcodes

          int mag=(q->GetPageNumber() >> 16) & 0x7;
          _pageList[mag].push_back(*q); // This copies. But we can't copy a mutex
      }
    }
	}
	closedir(dp);
  // std::cerr << "[PageList::LoadPageList]FINISHED LOADING PAGES" << std::endl;

	// How many files did we accept?
	for (int i=0;i<8;i++)
  {
    _mag[i]=new vbit::PacketMag(i, &_pageList[i], _configure, 9); // this creates the eight PacketMags that Service will use. Priority will be set in Service later
  }
  
  AddSpecialPagesAndCarousels(); // add any special pages that were loaded in
  
  // Just for testing
  if (1) for (int i=0;i<8;i++)
  {
    vbit::PacketMag* m=_mag[i];
    std::list<TTXPageStream>* p=m->Get_pageSet();
    for (std::list<TTXPageStream>::const_iterator it=p->begin();it!=p->end();++it)
    {
      if (&(*it)==NULL)
          std::cerr << "[PageList::LoadPageList] This can't happen unless something is broken" << std::endl;
       // std::cerr << "[PageList::LoadPageList]Dumping :" << std::endl;
       // it->DebugDump();
       std::cerr << "[PageList::LoadPageList] mag["<<i<<"] Filename =" << it->GetSourcePage()  << std::endl;
    }
  }

	return 0;
}

void PageList::AddPage(TTXPageStream* page)
{
		int mag=(page->GetPageNumber() >> 16) & 0x7;
		_pageList[mag].push_back(*page);
}


// Find a page by filename
TTXPageStream* PageList::Locate(std::string filename)
{
  // This is called from the FileMonitor thread
  // std::cerr << "[PageList::Locate] *** TODO *** " << filename << std::endl;
  for (int mag=0;mag<8;mag++)
  {
    //for (auto p : _pageList[mag])
    for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
    {
      TTXPageStream* ptr;
      ptr=&(*p);
      // std::cerr << "[PageList::Locate]scan:" << ptr->GetSourcePage() << std::endl;
      if (filename==ptr->GetSourcePage())
        return ptr;
    }

  }
 return NULL; // @todo placeholder What should we do here?
}

// Find a page by page number. THIS IS TO BE REPLACED BY Match().
TTXPageStream* PageList::FindPage(char* pageNumber)
{
	std::cerr << "[PageList::FindPage] Need to extend to multiple page selection" << std::endl;
	int mag=(pageNumber[0]-'0') & 0x07;
	// For each page
	for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
	{
		TTXPageStream* ptr;
		std::stringstream ss;
		char s[6];
		char* ps=s;
		bool match=true;
		ptr=&(*p);
		// Convert the page number into a string so we can compare it
		ss << std::hex << std::uppercase << std::setw(5) << ptr->GetPageNumber();
		strcpy(ps,ss.str().c_str());

		for (int i=0;i<5;i++)
    {
      if (pageNumber[i]!='*') // wildcard
      {
        if (pageNumber[i]!=s[i])
        {
          match=false;
        }
      }
    }
		if (match)
    {
			return ptr;
    }
  }
	return nullptr; // Not found

}

int PageList::Match(char* pageNumber)
{
  int matchCount=0;

 	std::cerr << "[PageList::FindPage] Selecting " << pageNumber << std::endl;
	int begin=0;
	int end=7;

	for (int mag=begin;mag<end+1;mag++)
  {
    // For each page
    for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
    {
      TTXPageStream* ptr;
      std::stringstream ss;
      char s[6];
      char* ps=s;
      bool match=true;
      for (ptr=&(*p);ptr!=nullptr;ptr=(TTXPageStream*)ptr->Getm_SubPage()) // For all the subpages in a carousel
      {
        // Convert the page number into a string so we can compare it
        ss << std::hex << std::uppercase << std::setw(5) << ptr->GetPageNumber();
        strcpy(ps,ss.str().c_str());
        // std::cerr << "[PageList::FindPage] matching " << ps << std::endl;

        for (int i=0;i<5;i++)
        {
          if (pageNumber[i]!='*') // wildcard
          {
            if (pageNumber[i]!=ps[i])
            {
              match=false;
            }
          }
        }
      }
      if (match)
      {
        matchCount++;
        // std::cerr << "[PageList::FindPage] MATCHED " << ps << std::endl;
      }
      ptr->SetSelected(match);
    }
  }
  // Set up the iterator for commands that use pages selected by the Page Identity
  _iterMag=0;
  _iter=_pageList[_iterMag].begin();

	return matchCount; // final count
}

TTXPageStream* PageList::NextPage()
{
  std::cerr << "[PageList::NextPage] looking for a selected page, mag=" << (int)_iterMag << std::endl;
  bool more=true;
  if (_iterSubpage!=nullptr)
  {
    std::cerr << "A";
    _iterSubpage=(TTXPageStream*) _iterSubpage->Getm_SubPage();
    std::cerr << "B";
  }
  if (_iterSubpage!=nullptr)
  {
    std::cerr << "C";
    return _iterSubpage;
  }
  std::cerr << "[PageList::NextPage] _iterSubpage is null, so checking next page" << std::endl;

  if (_iter!=_pageList[_iterMag].end())
  {
    ++_iter; // Next page
  }
  if (_iter==_pageList[_iterMag].end()) // end of mag?
  {
    if (_iterMag<7)
    {
      _iterMag++; // next mag
      _iter=_pageList[_iterMag].begin();
    }
    else
    {
      more=false; // End of last mag
    }
  }

  if (more)
  {
    _iterSubpage=&(*_iter);
    return _iterSubpage;
  }
  _iterSubpage=nullptr;
  return nullptr; // Returned after the last page is iterated
}

TTXPageStream* PageList::PrevPage()
{
  //std::cerr << "[PageList::NextPage] looking for a selected page, mag=" << (int)_iterMag << std::endl;
  bool more=true;
  if (_iter!=_pageList[_iterMag].begin())
  {
    --_iter; // Previous page
  }

  if (_iter==_pageList[_iterMag].begin()) // beginning of mag?
  {
    if (_iterMag<=0)
    {
      _iterMag--; // previous mag
      _iter=_pageList[_iterMag].end();
    }
    else
    {
      more=false;
    }
  }

  if (more)
  {
    return &(*_iter);
  }
  return nullptr; // Returned after the first page is iterated
}

TTXPageStream* PageList::FirstPage()
{
  // Reset the iterators
  _iterMag=0;
  _iter=_pageList[_iterMag].begin();
  _iterSubpage=&(*_iter);
  // Iterate through all the pages
  std::cerr << "[PageList::FirstPage] about to find if there is a selected page" << std::endl;
  for (TTXPageStream* p=_iterSubpage; p!=nullptr; p=NextPage())
  {
    if (p->Selected()) // If the page is selected, return a pointer to it
    {
      std::cerr << "[PageList::FirstPage] selected page found" << std::endl;
      return p;
    }
  }
  std::cerr << "[PageList::FirstPage] no selected page" << std::endl;
  return nullptr; // No selected page
}

TTXPageStream* PageList::LastPage()
{
  // Reset the iterators
  _iterMag=7;
  _iter=_pageList[_iterMag].end();
  // Iterate through all the pages
  for (TTXPageStream* p=&(*_iter); p!=nullptr; p=PrevPage())
  {
    if (p->Selected()) // If the page is selected, return a pointer to it
    {
      return p;
    }
  }
  return nullptr; // No selected page
}

TTXPageStream* PageList::NextSelectedPage()
{
  TTXPageStream* page;
  for(;;)
  {
    // std::cerr << "[PageList::NextSelectedPage] looking for a selected page, mag=" << (int)_iterMag << std::endl;
    page=NextPage();
    if (page==nullptr || page->Selected())
    {
      return page;
    }
  }
}

// Detect pages that have been deleted from the drive
// Do this by first clearing all the "exists" flags
// As we scan through the list, set the "exists" flag as we match up the drive to the loaded page
void PageList::ClearFlags()
{
  for (int mag=0;mag<8;mag++)
  {
    for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
    {
      TTXPageStream* ptr;
      ptr=&(*p);
      // Don't unmark a file that was MARKED. Once condemned it won't be pardoned
      if (ptr->GetStatusFlag()==TTXPageStream::FOUND)
      {
        ptr->SetState(TTXPageStream::NOTFOUND);
      }
    }
  }
}

void PageList::DeleteOldPages()
{
  // This is called from the FileMonitor thread
  for (int mag=0;mag<8;mag++)
  {
    for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
    {
      TTXPageStream* ptr;
      ptr=&(*p);
      if (ptr->GetStatusFlag()==TTXPageStream::NOTFOUND)
      {
				// std::cerr << "[PageList::DeleteOldPages] Marked for Delete " << ptr->GetSourcePage() << std::endl;
				// Pages marked here get deleted in the Service thread
        ptr->SetState(TTXPageStream::MARKED);
      }
    }
  }
}

void PageList::AddSpecialPagesAndCarousels()
{
  // moves any special pages and carousels into the magazine's _specialPages and _carousels lists immediately so that it doesn't have to wait for the page to next be transmitted
  for (int mag=0;mag<8;mag++)
  {
    for (std::list<TTXPageStream>::iterator p=_pageList[mag].begin();p!=_pageList[mag].end();++p)
    {
        TTXPageStream* ptr;
        ptr=&(*p);
        if (ptr->IsCarousel() && !(ptr->GetCarouselFlag()))
        {
            ptr->SetCarouselFlag(ptr->IsCarousel());
            _mag[mag]->GetCarousel()->addPage(ptr);
        }
        if (ptr->Special() && !(ptr->GetSpecialFlag()))
        {
            ptr->SetSpecialFlag(true);
            _mag[mag]->GetSpecialPages()->addPage(ptr);
        }
    }
  }
}

/* Want this to happen in the Service thread.
      // Not the best idea, to check for deletes here
      if (_fileToDelete==ptr->GetSourcePage()) // This works but isn't great. Probably not too efficient
      {
        std::cerr << "[PageList::Locate]ready to delete " << ptr->GetSourcePage() << std::endl;
        _fileToDelete="null"; // Signal that delete has been done.
        //@todo Delete the page object, remove it from pagelist, fixup mag, skip to the next page
        _pageList[mag].remove(*(p++)); // Also post-increment the iterator OR WE CRASH!
        // We can do this safely because this thread is in the Service thread and won't clash WRONG!
      }

*/
