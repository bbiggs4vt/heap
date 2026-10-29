#pragma once

#include <functional>
#include <memory>
#include <boost/weak_ptr.hpp>
#include "SortedHeapV2.h"

///
/// Version 3 of CSortedHeap and CSortedChangingHeap.
/// @details CSortedHeap is the same as v2. CSortedChangingHeap keeps the
/// same interface as v2 but finds the top item with a single
/// std::max_element pass instead of rebuilding a heap with
/// std::make_heap. Items in this container change value, enter and
/// leave between calls, and sort methods may depend on the last item
/// chosen (e.g. next frequency up), so any ordering kept from a previous
/// call is stale. A linear scan makes fewer comparisons than make_heap,
/// moves no items, and does not rely on a heap invariant.
/// When the items are weak pointers, Top also removes every expired
/// item in one pass first, rather than leaving the sort methods to move
/// them to the top to be popped and thrown out one search at a time.
namespace v3
{

/// unchanging heap is the same as v2
using v2::CSortedHeap;

namespace detail
{
	/// Items that are not weak pointers never expire
	template< typename T >
	bool IsExpired(const T&)
	{
		return(false);
	}

	/// Method that returns whether a weak pointer has expired
	template< typename U >
	bool IsExpired(const boost::weak_ptr<U>& item)
	{
		return(item.expired());
	}

	/// Method that returns whether a weak pointer has expired
	template< typename U >
	bool IsExpired(const std::weak_ptr<U>& item)
	{
		return(item.expired());
	}
}

///
/// Templated class to find the top item of a set of changing items
/// @details The items in this container can be changing value
/// thus the top item is found again each time Top or PopTop is called.
/// If T is a weak pointer, expired items are removed when Top is called.
template< typename T >
class CSortedChangingHeap
{
public:
	/// typedef for smart pointer
	typedef boost::shared_ptr< ISortMethod<T> > ISortMethodPtr;

	/// typedef of container
	typedef std::vector<T> HeapContainer;

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	CSortedChangingHeap()
		: topIndex(0), topValid(false)
	{

	}

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	/// @param [in] start number of items to reserve space for
	CSortedChangingHeap(size_t start)
		: topIndex(0), topValid(false)
	{
		container.reserve(start);
	}

	/// Constructor
	/// @param [in] newSorting sorting method
	CSortedChangingHeap(const ISortMethodPtr& newSorting)
		: topIndex(0), topValid(false)
	{
		SetSorting(newSorting);
	}

	/// operator to allow this class to be a comparitor
	/// @details defers to LessThan function of sorting method
	/// @param [in] lhs left hand side of operation
	/// @param [in] rhs right hand side of operation
	/// @return same as LessThan function
	bool operator()(const T& lhs, const T& rhs) const
	{
		assert(sorting);

		return(sorting->LessThan(lhs, rhs));
	};

	/// Method to set the sorting for the heap
	/// @param [in] newSorting sorting method to use
	void SetSorting(const ISortMethodPtr& newSorting)
	{
		sorting=newSorting;
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
	/// @details removes expired weak pointers, then finds the top item
	/// using the current values of all items and the current state of
	/// the sorting method. This method will not remove the top item.
	/// An item can still expire after this check, so callers should
	/// still throw out an item that fails to lock.
	/// @return top item, or an empty item if every item had expired
	T Top()
	{
		assert(!container.empty());

		RemoveExpired();
		if(container.empty())
		{
			topValid=false;
			return(T());
		}

		topIndex=std::max_element(container.begin(), container.end(), std::cref(*this)) - container.begin();
		topValid=true;

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
	/// Method to remove expired weak pointers in one pass
	void RemoveExpired()
	{
		container.erase(std::remove_if(container.begin(), container.end(),
			[](const T& item) { return(detail::IsExpired(item)); }), container.end());
	}

	HeapContainer container; //<! container of items
	ISortMethodPtr sorting; //!< sorting method
	size_t topIndex; //!< index of the top item found by the last Top
	bool topValid; //!< true if topIndex is valid
};

} // namespace v3
