/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "common/config-manager.h"
#include "common/file.h"
#include "common/substream.h"
#include "common/macresman.h"

#include "director/director.h"
#include "director/archive.h"
#include "director/util.h"

namespace Director {

// Base Archive code

Archive::Archive() {
	_stream = 0;
	_isBigEndian = true;
}

Archive::~Archive() {
	close();
}

Common::String Archive::getFileName() const { return Director::getFileName(_pathName); }

bool Archive::openFile(const Common::String &fileName) {
	Common::File *file = new Common::File();

	if (!file->open(fileName)) {
		warning("Archive::openFile(): Error opening file %s", fileName.c_str());
		delete file;
		return false;
	}

	_pathName = fileName;

	if (!openStream(file)) {
		warning("Archive::openFile(): Error loading stream from file %s", fileName.c_str());
		close();
		return false;
	}

	return true;
}

void Archive::close() {
	_types.clear();

	if (_stream)
		delete _stream;

	_stream = 0;
}

int Archive::getFileSize() {
	if (!_stream)
		return 0;

	return _stream->size();
}

bool Archive::hasResource(uint32 tag, int id) const {
	if (!_types.contains(tag))
		return false;

	if (id == -1)
		return true;

	return _types[tag].contains(id);
}

bool Archive::hasResource(uint32 tag, const Common::String &resName) const {
	if (!_types.contains(tag) || resName.empty())
		return false;

	const ResourceMap &resMap = _types[tag];

	for (ResourceMap::const_iterator it = resMap.begin(); it != resMap.end(); it++)
		if (it->_value.name.matchString(resName))
			return true;

	return false;
}

Common::SeekableSubReadStreamEndian *Archive::getFirstResource(uint32 tag) {
	return getResource(tag, getResourceIDList(tag)[0]);
}

Common::SeekableSubReadStreamEndian *Archive::getResource(uint32 tag, uint16 id) {
	if (!_types.contains(tag))
		error("Archive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("Archive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const Resource &res = resMap[id];

	return new Common::SeekableSubReadStreamEndian(_stream, res.offset, res.offset + res.size, _isBigEndian, DisposeAfterUse::NO);
}

Resource Archive::getResourceDetail(uint32 tag, uint16 id) {
	if (!_types.contains(tag))
		error("Archive::getResourceDetail(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("Archive::getResourceDetail(): Archive does not contain '%s' %d", tag2str(tag), id);

	return resMap[id];
}

uint32 Archive::getOffset(uint32 tag, uint16 id) const {
	if (!_types.contains(tag))
		error("Archive::getOffset(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("Archive::getOffset(): Archive does not contain '%s' %d", tag2str(tag), id);

	return resMap[id].offset;
}

uint16 Archive::findResourceID(uint32 tag, const Common::String &resName) const {
	if (!_types.contains(tag) || resName.empty())
		return 0xFFFF;

	const ResourceMap &resMap = _types[tag];

	for (ResourceMap::const_iterator it = resMap.begin(); it != resMap.end(); it++)
		if (it->_value.name.matchString(resName))
			return it->_key;

	return 0xFFFF;
}

Common::String Archive::getName(uint32 tag, uint16 id) const {
	if (!_types.contains(tag))
		error("Archive::getName(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("Archive::getName(): Archive does not contain '%s' %d", tag2str(tag), id);

	return resMap[id].name;
}

Common::Array<uint32> Archive::getResourceTypeList() const {
	Common::Array<uint32> typeList;

	for (TypeMap::const_iterator it = _types.begin(); it != _types.end(); it++)
		typeList.push_back(it->_key);

	return typeList;
}

Common::Array<uint16> Archive::getResourceIDList(uint32 type) const {
	Common::Array<uint16> idList;

	if (!_types.contains(type))
		return idList;

	const ResourceMap &resMap = _types[type];

	for (ResourceMap::const_iterator it = resMap.begin(); it != resMap.end(); it++)
		idList.push_back(it->_key);

	return idList;
}

uint32 Archive::convertTagToUppercase(uint32 tag) {
	uint32 newTag = toupper(tag >> 24) << 24;
	newTag |= toupper((tag >> 16) & 0xFF) << 16;
	newTag |= toupper((tag >> 8) & 0xFF) << 8;

	return newTag | toupper(tag & 0xFF);
}

// Mac Archive code

MacArchive::MacArchive() : Archive(), _resFork(0) {
}

MacArchive::~MacArchive() {
	delete _resFork;
}

void MacArchive::close() {
	Archive::close();
	delete _resFork;
	_resFork = 0;
}

bool MacArchive::openFile(const Common::String &fileName) {
	close();

	_resFork = new Common::MacResManager();

	Common::String fName = fileName;

	if (!_resFork->open(fName) || !_resFork->hasResFork()) {
		close();
		return false;
	}

	_pathName = _resFork->getBaseFileName();
	if (_pathName.hasSuffix(".bin")) {
		for (int i = 0; i < 4; i++)
			_pathName.deleteLastChar();
	}

	readTags();

	return true;
}

bool MacArchive::openStream(Common::SeekableReadStream *stream, uint32 startOffset) {
	close();

	if (startOffset)
		error("MacArchive::openStream(): startOffset > 0 is not yet implemented");

	_resFork = new Common::MacResManager();
	stream->seek(startOffset);

	if (!_resFork->loadFromMacBinary(*stream)) {
		warning("MacArchive::openStream(): Error loading Mac Binary");
		close();
		return false;
	}

	_pathName = "<stream>";
	_resFork->setBaseFileName(_pathName);

	readTags();

	return true;
}

void MacArchive::readTags() {
	Common::MacResTagArray tagArray = _resFork->getResTagArray();

	for (uint32 i = 0; i < tagArray.size(); i++) {
		ResourceMap &resMap = _types[tagArray[i]];
		Common::MacResIDArray idArray = _resFork->getResIDArray(tagArray[i]);

		for (uint32 j = 0; j < idArray.size(); j++) {
			Resource &res = resMap[idArray[j]];

			res.offset = res.size = 0; // unused
			res.name = _resFork->getResName(tagArray[i], idArray[j]);
			debug(3, "Found MacArchive resource '%s' %d: %s", tag2str(tagArray[i]), idArray[j], res.name.c_str());
		}
	}
}

Common::SeekableSubReadStreamEndian *MacArchive::getResource(uint32 tag, uint16 id) {
	assert(_resFork);
	Common::SeekableReadStream *stream = _resFork->getResource(tag, id);

	if (stream == nullptr) {
		warning("MacArchive::getResource('%s', %d): Resource doesn't exit", tag2str(tag), id);
		return nullptr;
	}

	return new Common::SeekableSubReadStreamEndian(stream, 0, stream->size(), true, DisposeAfterUse::NO);
}

// RIFF Archive code

bool RIFFArchive::openStream(Common::SeekableReadStream *stream, uint32 startOffset) {
	close();

	_startOffset = startOffset;

	stream->seek(startOffset);

	if (convertTagToUppercase(stream->readUint32BE()) != MKTAG('R', 'I', 'F', 'F')) {
		warning("RIFFArchive::openStream(): RIFF expected but not found");
		return false;
	}

	stream->readUint32LE(); // size

	if (convertTagToUppercase(stream->readUint32BE()) != MKTAG('R', 'M', 'M', 'P')) {
		warning("RIFFArchive::openStream(): RMMP expected but not found");
		return false;
	}

	if (convertTagToUppercase(stream->readUint32BE()) != MKTAG('C', 'F', 'T', 'C')) {
		warning("RIFFArchive::openStream(): CFTC expected but not found");
		return false;
	}

	uint32 cftcSize = stream->readUint32LE();
	uint32 startPos = stream->pos();
	stream->readUint32LE(); // unknown (always 0?)

	while ((uint32)stream->pos() < startPos + cftcSize) {
		uint32 tag = convertTagToUppercase(stream->readUint32BE());

		uint32 size = stream->readUint32LE();
		uint32 id = stream->readUint32LE();
		uint32 offset = stream->readUint32LE();

		if (tag == 0)
			break;

		uint32 startResPos = stream->pos();
		stream->seek(startOffset + offset + 12);

		Common::String name = "";
		byte nameSize = stream->readByte();

		if (nameSize) {
			for (uint8 i = 0; i < nameSize; i++) {
				name += stream->readByte();
			}
		}

		stream->seek(startResPos);

		debug(3, "Found RIFF resource '%s' %d: %d @ 0x%08x (0x%08x)", tag2str(tag), id, size, offset, startOffset + offset);

		ResourceMap &resMap = _types[tag];
		Resource &res = resMap[id];
		res.offset = offset;
		res.size = size;
		res.name = name;
		res.tag = tag;
	}

	_stream = stream;
	return true;
}

Common::SeekableSubReadStreamEndian *RIFFArchive::getResource(uint32 tag, uint16 id) {
	if (!_types.contains(tag))
		error("RIFFArchive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("RIFFArchive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const Resource &res = resMap[id];

	// Adjust to skip the resource header
	uint32 offset = res.offset + 12;
	uint32 size = res.size - 4;
	// Skip the Pascal string
	_stream->seek(_startOffset + offset);
	byte stringSize = _stream->readByte(); // 1 for this byte

	offset += stringSize + 1;
	size -= stringSize + 1;

	// Align to nearest word boundary
	if (offset & 1) {
		offset++;
		size--;
	}

	return new Common::SeekableSubReadStreamEndian(_stream, _startOffset + offset, _startOffset + offset + size, true, DisposeAfterUse::NO);
}

// RIFX Archive code
bool RIFXArchive::openStream(Common::SeekableReadStream *stream, uint32 startOffset) {
	close();

	stream->seek(startOffset);

	uint32 moreOffset = 0;

	uint32 headerTag = stream->readUint32BE();

	if (headerTag != MKTAG('R', 'I', 'F', 'X') &&
		headerTag != MKTAG('X', 'F', 'I', 'R')) {
		// Check if it is MacBinary

		stream->seek(startOffset);

		if (Common::MacResManager::isMacBinary(*stream)) {
			warning("RIFXArchive::openStream(): MacBinary detected, overriding");

			moreOffset = Common::MacResManager::getDataForkOffset();
			stream->seek(startOffset + moreOffset);

			headerTag = stream->readUint32BE();
		}
	}

	if (headerTag == MKTAG('R', 'I', 'F', 'X')) {
		_isBigEndian = true;
	} else if (SWAP_BYTES_32(headerTag) == MKTAG('R', 'I', 'F', 'X')) {
		_isBigEndian = false;
	} else {
		warning("RIFXArchive::openStream(): RIFX or XFIR expected but %s found", tag2str(headerTag));
		return false;
	}

	Common::SeekableSubReadStreamEndian subStream(stream, startOffset + 4 + moreOffset, stream->size(), _isBigEndian, DisposeAfterUse::NO);

	uint32 sz = subStream.readUint32(); // size

	// If it is an embedded file, dump it if requested
	if (ConfMan.getBool("dump_scripts") && startOffset) {
		Common::DumpFile out;

		char buf[256];
		sprintf(buf, "./dumps/%s-%08x", g_director->getEXEName().c_str(), startOffset);

		if (out.open(buf, true)) {
			byte *data = (byte *)malloc(sz);

			stream->seek(startOffset);
			stream->read(data, sz);
			out.write(data, sz);
			out.flush();
			out.close();

			free(data);

			stream->seek(startOffset + 8);
		} else {
			warning("RIFXArchive::openStream(): Can not open dump file %s", buf);
		}
	}

	uint32 rifxType = subStream.readUint32();

	if (rifxType != MKTAG('M', 'V', '9', '3') &&
		rifxType != MKTAG('A', 'P', 'P', 'L') &&
		rifxType != MKTAG('M', 'C', '9', '5'))
		return false;

	if (subStream.readUint32() != MKTAG('i', 'm', 'a', 'p'))
		return false;

	subStream.readUint32(); // imap length
	subStream.readUint32(); // unknown
	uint32 mmapOffset = subStream.readUint32() - startOffset - 4;
	uint32 version = subStream.readUint32(); // 0 for 4.0, 0x4c1 for 5.0, 0x4c7 for 6.0, 0x708 for 8.5, 0x742 for 10.0
	warning("RIFX: version: %x type: %s", version, tag2str(rifxType));

	subStream.seek(mmapOffset);

	if (subStream.readUint32() != MKTAG('m', 'm', 'a', 'p')) {
		warning("RIFXArchive::openStream(): mmap expected but not found");
		return false;
	}

	subStream.readUint32(); // mmap length
	subStream.readUint16(); // unknown
	subStream.readUint16(); // unknown
	subStream.readUint32(); // resCount + empty entries
	uint32 resCount = subStream.readUint32();
	subStream.skip(8); // all 0xFF
	subStream.readUint32(); // unknown

	Common::Array<Resource *> resources;
	resources.reserve(resCount);

	// Need to look for these two resources
	const Resource *keyRes = 0;
	const Resource *casRes = 0;

	for (uint32 i = 0; i < resCount; i++) {
		uint32 tag = subStream.readUint32();
		uint32 size = subStream.readUint32();
		uint32 offset = subStream.readUint32() + moreOffset;
		uint16 flags = subStream.readUint16();
		uint16 unk1 = subStream.readUint16();
		uint32 unk2 = subStream.readUint32();

		debug(3, "Found RIFX resource index %d: '%s', %d bytes @ 0x%08x (%d), flags: %x unk1: %x unk2: %x",
			i, tag2str(tag), size, offset, offset, flags, unk1, unk2);

		// APPL is a special case; it has an embedded "normal" archive
		if (rifxType == MKTAG('A', 'P', 'P', 'L') && tag == MKTAG('F', 'i', 'l', 'e'))
			return openStream(stream, offset);

		Resource &res = _types[tag][i];
		res.index = i;
		res.offset = offset;
		res.size = size;
		res.tag = tag;
		resources.push_back(&res);

		// Looking for two types here
		if (tag == MKTAG('K', 'E', 'Y', '*'))
			keyRes = &res;
		else if (tag == MKTAG('C', 'A', 'S', '*'))
			casRes = &res;
	}

	if (ConfMan.getBool("dump_scripts")) {
		debug("RIFXArchive::openStream(): Dumping %d resources", resources.size());

		byte *data = nullptr;
		uint dataSize = 0;
		Common::DumpFile out;

		for (uint i = 0; i < resources.size(); i++) {
			stream->seek(resources[i]->offset);

			uint32 len = resources[i]->size;

			if (dataSize < resources[i]->size) {
				free(data);
				data = (byte *)malloc(resources[i]->size);
				dataSize = resources[i]->size;
			}
			Common::String prepend;
			if (_pathName.size() != 0)
				prepend = _pathName;
			else
				prepend = "stream";

			Common::String filename = Common::String::format("./dumps/%s-%s-%d", prepend.c_str(), tag2str(resources[i]->tag), i);
			stream->read(data, len);

			if (!out.open(filename, true)) {
				warning("RIFXArchive::openStream(): Can not open dump file %s", filename.c_str());
				break;
			}

			out.write(data, len);

			out.flush();
			out.close();
		}
	}



	// We need to have found the 'File' resource already
	if (rifxType == MKTAG('A', 'P', 'P', 'L')) {
		warning("No 'File' resource present in APPL archive");
		return false;
	}

	// A KEY* must be present
	if (!keyRes) {
		warning("No 'KEY*' resource present");
		return false;
	}

	uint castTag = MKTAG('C', 'A', 'S', 't');

	// Parse the CAS*, if present
	if (casRes) {
		Common::SeekableSubReadStreamEndian casStream(stream, casRes->offset + 8, casRes->offset + 8 + casRes->size, _isBigEndian, DisposeAfterUse::NO);

		uint casSize = casRes->size / 4;

		debugCN(2, kDebugLoading, "CAS*: %d [", casSize);

		for (uint i = 0; i < casSize; i++) {
			uint32 castIndex = casStream.readUint32BE();
			debugCN(2, kDebugLoading, "%d ", castIndex);

			if (castIndex == 0) {
				continue;
			}
			Resource &res = _types[castTag][castIndex];
			res.castId = i;
		}
		debugC(2, kDebugLoading, "]");
	}

	// Parse the KEY*
	Common::SeekableSubReadStreamEndian keyStream(stream, keyRes->offset + 8, keyRes->offset + 8 + keyRes->size, _isBigEndian, DisposeAfterUse::NO);
	uint16 entrySize = keyStream.readUint16(); // Should always be 12 (3 uint32's)
	uint16 entrySize2 = keyStream.readUint16();
	uint32 entryCount = keyStream.readUint32(); // There are more entries than actually used
	uint32 usedCount = keyStream.readUint32();

	debugC(2, kDebugLoading, "KEY*: entrySize: %d entrySize2: %d entryCount: %d usedCount: %d", entrySize, entrySize2, entryCount, usedCount);

	for (uint16 i = 0; i < usedCount; i++) {
		uint32 childIndex = keyStream.readUint32();
		uint32 parentIndex = keyStream.readUint32();
		uint32 childTag = keyStream.readUint32();

		debugC(2, kDebugLoading, "KEY*: childIndex: %d parentIndex: %d childTag: %s", childIndex, parentIndex, tag2str(childTag));
		if (childIndex < resources.size() && parentIndex < resources.size())
			resources[parentIndex]->children.push_back(*resources[childIndex]);
	}

	_stream = stream;
	return true;
}

Common::SeekableSubReadStreamEndian *RIFXArchive::getResource(uint32 tag, uint16 id) {
	if (!_types.contains(tag))
		error("RIFXArchive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("RIFXArchive::getResource(): Archive does not contain '%s' %d", tag2str(tag), id);

	const Resource &res = resMap[id];

	uint32 offset = res.offset + 8;
	uint32 size = res.size;

	return new Common::SeekableSubReadStreamEndian(_stream, offset, offset + size, true, DisposeAfterUse::NO);
}

Resource RIFXArchive::getResourceDetail(uint32 tag, uint16 id) {
	if (!_types.contains(tag))
		error("RIFXArchive::getResourceDetail(): Archive does not contain '%s' %d", tag2str(tag), id);

	const ResourceMap &resMap = _types[tag];

	if (!resMap.contains(id))
		error("RIFXArchive::getResourceDetail(): Archive does not contain '%s' %d", tag2str(tag), id);

	return resMap[id];
}


} // End of namespace Director
