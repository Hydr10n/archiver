#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#include "archiver.hpp"

#include <iostream>

using namespace std;
using namespace std::chrono;
using namespace std::filesystem;
using namespace Hydr10n::File;

#define HANDLE_ERROR() { perror("Error"); return errno; }

ostream& operator<<(ostream& os, const file_time_type& file_time) {
	const auto time = system_clock::to_time_t(time_point_cast<system_clock::duration>(file_time - file_time_type::clock::now() + system_clock::now()));
	return os << "Last write time: " << asctime(localtime(&time));
}

bool on_error_occured(const path& path, void* param) {
	perror("Error");
	return false;
}

bool on_entering_directory(const path& path, const archiver::find_file::data& data, void* param) {
	cout << path.string() << data.filename << static_cast<char>(path::preferred_separator) << endl
		<< data.last_write_time
		<< endl;
	return true;
}

bool on_file_found(const path& path, const archiver::find_file::data& data, void* param) {
	cout << path.string() << data.filename << endl
		<< data.last_write_time
		<< "Size: " << data.file_size << ' ' << (data.file_size == 1 ? "byte" : "bytes") << endl
		<< endl;
	return true;
}

int main(int argc, char** argv) {
	string dst_path;
	if (argc < 2) {
		cout << "Destination path: ";
		getline(cin, dst_path);
		cout << endl;
	}
	else dst_path = argv[1];

	string src_path;

	bool file_exists;
	if (!(file_exists = exists(dst_path))) {
		if (argc < 3) {
			cout << "Source path: ";
			getline(cin, src_path);
			cout << endl;
		}
		else src_path = argv[2];
	}

	try {
		archiver archiver(dst_path);

		if (!file_exists) {
			cout << "Archiving..." << endl;

			if (!archiver.archive(src_path, on_error_occured, on_file_found, on_entering_directory, nullptr)) HANDLE_ERROR();

			cout << "----------------" << endl << endl;
		}
		cout << "Listing contents..." << endl;

		if (!archiver.find_files(initializer_list<char>{ path::preferred_separator, 0 }.begin(), on_error_occured, on_file_found, on_entering_directory, nullptr, nullptr)) HANDLE_ERROR();
	}
	catch (const ios_base::failure&) { HANDLE_ERROR(); }
	catch (const runtime_error& e) {
		cerr << endl << "Error: " << e.what() << endl;

		return EXIT_FAILURE;
	}
	catch (const exception& e) {
		cerr << endl << e.what() << endl;

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
