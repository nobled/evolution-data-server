
This version of camel is working towards being multi-thread safe
(MT-SAFE).  At least, for the important api's.

Intended method
===============

I intend working on it in several stages:

1. Making the api multi-threadable.  Basically removing some const-returns,
and copying some data where it wasn't before.  The api should
still continue to work if not being used in a multithreaded
application.  There is not a significant amount of work here since
this was more or less the intention all along.

Some functions where references to objects are returned may have to be
changed slightly, so that refcounts are incremented before return.
This doesn't affect much though.

camel_folder::get_message_info				done
camel_folder_summary::uid				done
camel_folder_summary::index				done
camel_folder::get_summary
	Needs to ref each summary item it points to.
camel_folder::free_summary
	Needs to unref each summary item it points to.
camel_folder_get_message_tag
	needs to copy the tag return
camel_maildir_summary filename string
	should not be able to modify the string
	array contents after it has been added to
	the summary.
camel_folder
	Make every camel-folder use a camel-folder-summary.
	This just reduces some of the code duplication,
	since everything but vee-folder does this already.

2. Adding high level locks for proof of concept.  The locks will
be stored in private or global data, so the api should remain the same for
non-threaded applications.

A per-folder lock which governs access to the folder
	summary, the folder file or
	communications socket, etc.			done
Locking for exceptions.
Per store locks for internal stuff.			done
Per-service locks for various internal lists and
	caches						done

3. Further fine-grained locking where it can be done/is worthwhile.

A per-index lock for libibex				done
Internal lock for the folder_summary itself
	So that searching can be detatched from other
	folder operations, etc.
Possibly a lock for access to parts of a mime-part
	or message

4. A method to cancel operations.

Individual outstanding operations must be cancellable, and not just
'all current operations'.  This will probably not use pthread_cancel
type of cancelling.

This will however, probably use a method for starting a new thread,
through camel, that can then be cancelled, and/or some method of
registering that a thread can be cancelled.  Blocking states within
camel, within that thread, will then act as checkpoints for if the
operation, and if it is cancelled, the operation will abort
(i.e. fail, with an appropriate exception code).

Operation cancelling should also function when the application is not
multi-threaded.  Not sure of the api for this yet, probably a callback
system.  Hopefully the api for both scenarios can be made the same.

Other thoughts
==============

Basically much of the code in camel that does the actual work does NOT
need to be thread safe to make it safely usable in an mt context.

camel-folder, camel-summary, camel-imap-search, and the camel-service
classes (at least) are the important ones to be made multithreaded.

For other things, they are either resources that are created
one-off (for example, camel-mime-message, and its associated
parts, like camel-internet-address), or multithreadedness
doesn't make a lot of sense - e.g. camel-stream, or camel-mime-parser.

So basically the approach is a low-risk one.  Adding the minimum
number of locks to start with, and providing further fine-grained
locks as required.  The locks should not need to be particularly
fine-grained in order to get reasonable results.

Log of changes
==============

Changed CamelFolder:get_message_info() to return a ref'd copy, requiring
all get_message_info()'s to have a matching free_message_info().

Moved the CamelFolder frozen changelog data to a private structure.

Added a mutex for CamelFolder frozen changelog stuff (it was just easy
to do, although it isn't needed yet).

Added a single mutex around all other CamelFolder functions that need
it, this is just the first cut at mt'edness.

Fixed all camel-folder implementations that call any other
camel-folder functions to call via virtual methods, to bypass the locks.

Added camel-store private data.

Added a single mutex lock for camel-store's folder functions.

Added camel-service private data.

Added a single mutex lock for camel-service's connect stuff.

Added a mutex for remote-store stream io stuff.

Added a mutex for imap, so it can bracket a compound command
exclusively.  Pop doesn't need this since you can only have a single
folder per store, and the folder interface is already forced
single-threaded.

Added mutex for camel-session, most operations.

Running the tests finds at least 1 deadlock so far.  Need to
work on that.
