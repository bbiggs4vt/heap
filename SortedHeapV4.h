#pragma once

#include "SortedHeapV3.h"

///
/// Version 4 adds CSortedChangingWeakHeap.
/// @details CSortedHeap and CSortedChangingHeap are the same as v3.
/// CSortedChangingWeakHeap holds weak pointers but sorts with a sort
/// method on the shared pointers they lock to, so each item is locked
/// once per Top call instead of twice per comparison, and an item cannot
/// expire part way through a search.
namespace v4
{

/// unchanged from v3
using v3::CSortedHeap;

/// unchanged from v3
using v3::CSortedChangingHeap;

///
/// Templated class to find the top item of a set of changing items
/// held by weak pointer
/// @details Same interface as CSortedChangingHeap< boost::weak_ptr<U> >
/// except that the sorting method compares boost::shared_ptr<U>. A wrapper
/// sort method that locks the weak pointers and calls a shared pointer
/// sort method is not needed; pass the shared pointer sort method here.
/// Each Top call locks every item once, removes items that have expired,
/// and compares the locked pointers. Only the greatest item so far is
/// held locked during the search, and PopTopLocked hands it to the caller,
/// so this class never keeps items alive after a call returns.
///
/// An optional filter can be set. It is called once per live item per
/// search, and filtered items take no part in the comparisons. Filtered
/// items rank below every unfiltered item: if every live item is
/// filtered, Top returns one of them, so a caller that checks its own
/// filter on the top item behaves as it would with the filter as the
/// first sort criteria. Filtered items stay in the container.
template< typename U >
class CSortedChangingWeakHeap
{
public:
	/// typedef of item held
	typedef boost::weak_ptr<U> T;

	/// typedef of locked item
	typedef boost::shared_ptr<U> LockedT;

	/// typedef for smart pointer to the sort method on locked items
	typedef boost::shared_ptr< ISortMethod<LockedT> > ISortMethodPtr;

	/// typedef of filter, returns true if a locked item is filtered out
	typedef boost::function<bool (const LockedT&)> FilterFunction;

	/// typedef of container
	typedef std::vector<T> HeapContainer;

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	CSortedChangingWeakHeap()
		: topIndex(0), topValid(false)
	{

	}

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	/// @param [in] start number of items to reserve space for
	CSortedChangingWeakHeap(size_t start)
		: topIndex(0), topValid(false)
	{
		container.reserve(start);
	}

	/// Constructor
	/// @param [in] newSorting sorting method on locked items
	CSortedChangingWeakHeap(const ISortMethodPtr& newSorting)
		: topIndex(0), topValid(false)
	{
		SetSorting(newSorting);
	}

	/// Method to set the sorting
	/// @param [in] newSorting sorting method on locked items
	void SetSorting(const ISortMethodPtr& newSorting)
	{
		sorting=newSorting;
		topValid=false;
	}

	/// Method to set the filter
	/// @param [in] newFilter filter, or an empty function for no filter
	void SetFilter(const FilterFunction& newFilter)
	{
		filter=newFilter;
		topValid=false;
	}

	/// Method to remove all items
	void Clear()
	{
		container.clear();
		topValid=false;
	}

	/// Method to add an item
	/// @param [in] item item to add
	void AddItem(const T& item)
	{
		container.push_back(item);
		topValid=false;
	}

	/// Method to add an array of items
	/// @param [in] items items to add
	void AddItems(const std::vector<T>& items)
	{
		container.insert(container.end(), items.begin(), items.end());
		topValid=false;
	}

	/// Method to get the top item
	/// @details locks every item once, removes items that have expired,
	/// then finds the top item using the current values of all items and
	/// the current state of the sorting method. This method will not
	/// remove the top item. The item can still expire after the locks are
	/// released, use PopTopLocked to avoid that.
	/// @return top item, or an empty item if every item had expired
	T Top()
	{
		assert(!container.empty());

		FindTop();
		if(!topValid)
		{
			return(T());
		}

		return(container[topIndex]);
	}

	/// Method to get the top item and remove it
	/// @return top item, or an empty item if every item had expired
	T PopTop()
	{
		// get the value
		T value = Top();

		// Remove it, unless every item had expired
		if(topValid)
		{
			Pop();
		}

		return(value);
	}

	/// Method to get the top item locked and remove it
	/// @details the returned pointer was locked during the search, so
	/// it cannot have expired. Callers do not need to throw out expired
	/// items.
	/// @return top item, or null if every item had expired
	LockedT PopTopLocked()
	{
		assert(!container.empty());

		LockedT top=FindTop();
		if(topValid)
		{
			Pop();
		}

		return(top);
	}

	/// Method to get the item found by the last call to Top or PopTop
	/// @details does not search again. If items were added or removed
	/// since then, the top item is found again. Changes made through
	/// GetContainer are not tracked, so call Top after making them.
	/// @return top item
	T TopNoSort()
	{
		if(!topValid || topIndex >= container.size())
		{
			return(Top());
		}

		return(container[topIndex]);
	}

	/// Method to get the item found by the last call to Top or PopTop
	/// and remove it
	/// @details see TopNoSort
	/// @return top item
	T PopTopNoSort()
	{
		// get the value
		T value = TopNoSort();

		// Remove it, unless every item had expired
		if(topValid)
		{
			Pop();
		}

		return(value);
	}

	/// Method used to remove the top item
	/// @details removes the item found by the last call to Top,
	/// finding it first if needed. Order of the other items is
	/// not preserved.
	void Pop()
	{
		if(!topValid || topIndex >= container.size())
		{
			Top();
			if(!topValid)
			{
				return;
			}
		}

		// Swap the top item to the back so removing it is cheap
		if(topIndex != container.size() - 1)
		{
			using std::swap;
			swap(container[topIndex], container.back());
		}
		container.pop_back();
		topValid=false;
	}

	/// Method that returns whether or not the container is empty
	/// @return true for empty, false otherwise
	bool IsEmpty()
	{
		return(container.empty());
	}

	/// Method that returns the size of the underlying container
	/// size_t size of container
	size_t Size()
	{
		return(container.size());
	}

	/// Method used to get to the underlying container
	/// @details items may be added, removed or reordered through this
	/// reference. Call Top afterwards before using TopNoSort or Pop.
	/// reference to underlying container
	HeapContainer& GetContainer()
	{
		return(container);
	}

private:
	/// Method to lock every item once, remove expired items, and find
	/// the top item
	/// @details sets topIndex and topValid
	/// @return the top item locked, or null if every item had expired
	LockedT FindTop()
	{
		assert(sorting);

		// One pass: lock each item once, remove the ones that have
		// expired, skip filtered ones, and keep the greatest so far
		// locked. Same result as std::max_element, the first greatest item.
		LockedT top;
		size_t filteredIndex=0;
		bool anyFiltered=false;
		size_t kept=0;
		for(size_t i=0; i < container.size(); i++)
		{
			LockedT item=container[i].lock();
			if(!item)
			{
				continue;
			}

			if(kept != i)
			{
				container[kept]=container[i];
			}
			if(filter && filter(item))
			{
				// remember the first filtered item in case all are
				if(!anyFiltered)
				{
					filteredIndex=kept;
					anyFiltered=true;
				}
			}
			else if(!top || sorting->LessThan(top, item))
			{
				top.swap(item);
				topIndex=kept;
			}
			kept++;
		}
		container.resize(kept);

		// every live item is filtered, return one of them
		if(!top && anyFiltered)
		{
			topIndex=filteredIndex;
			top=container[topIndex].lock();
		}

		topValid=(top ? true : false);
		return(top);
	}

	HeapContainer container; //<! container of items
	ISortMethodPtr sorting; //!< sorting method on locked items
	FilterFunction filter; //!< filter, empty for none
	size_t topIndex; //!< index of the top item found by the last Top
	bool topValid; //!< true if topIndex is valid
};

} // namespace v4
