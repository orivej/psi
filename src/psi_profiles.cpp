/*
 * profiles.cpp - deal with profiles
 * Copyright (C) 2001-2003  Justin Karneges
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
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "profiles.h"
#include "common.h"
#include "applicationinfo.h"
#include <QDir>
#include <QFileInfo>
#include <QDomElement>

#include <QApplication>
#include <QTextStream>
#include <QtCrypto>
#include <QList>
#include <QUuid>
#include <QDesktopWidget>

#include "eventdlg.h"
#include "chatdlg.h"
#include "xmpp_xmlcommon.h"
#include "fancylabel.h"
#include "advwidget.h"
#include "psioptions.h"
#include "varlist.h"
#include "atomicxmlfile/atomicxmlfile.h"
#include "psitoolbar.h"
#include "optionstree.h"
#ifdef HAVE_PGPUTIL
#include "pgputil.h"
#endif
#ifdef PSI_PLUGINS
#include "pluginmanager.h"
#endif

using namespace XMPP;
using namespace XMLHelper;

#define PROXY_NONE       0
#define PROXY_HTTPS      1
#define PROXY_SOCKS4     2
#define PROXY_SOCKS5     3

template<typename T, typename F>
void migrateEntry(const QDomElement& element, const QString& entry, const QString& option, F f)
{
	if (!element.firstChildElement(entry).isNull()) {
		T value;
		f(element, entry, &value);
		PsiOptions::instance()->setOption(option, value);
	}
}

void migrateIntEntry(const QDomElement& element, const QString& entry, const QString& option)
{
	migrateEntry<int>(element, entry, option, readNumEntry);
}

void migrateBoolEntry(const QDomElement& element, const QString& entry, const QString& option)
{
	migrateEntry<bool>(element, entry, option, readBoolEntry);
}

void migrateSizeEntry(const QDomElement& element, const QString& entry, const QString& option)
{
	migrateEntry<QSize>(element, entry, option, readSizeEntry);
}

void migrateStringEntry(const QDomElement& element, const QString& entry, const QString& option)
{
	migrateEntry<QString>(element, entry, option, readEntry);
}

void migrateStringList(const QDomElement& element, const QString& entry, const QString& option)
{
	migrateEntry<QStringList>(element, entry, option, xmlToStringList);
}

void migrateColorEntry(const QDomElement& element, const QString &entry, const QString &option)
{
	migrateEntry<QColor>(element, entry, option, readColorEntry);
}

void migrateRectEntry(const QDomElement& element, const QString &entry, const QString &option)
{
	migrateEntry<QRect>(element, entry, option, readRectEntry);
}



UserAccount::UserAccount()
	: lastStatus(XMPP::Status::Online)
{
	reset();
}

void UserAccount::reset()
{
	id = QUuid::createUuid().toString();
	name = "Default";
	opt_enabled = true;
	opt_auto = false;
	tog_offline = true;
	tog_away = true;
	tog_hidden = false;
	tog_agents = true;
	tog_self = false;
	customAuth = false;
	storeSaltedHashedPassword = false;
	req_mutual_auth = false;
	legacy_ssl_probe = false;
	security_level = QCA::SL_None;
	ssl = SSL_Auto;
	jid = "";
	pass = "";
	scramSaltedHashPassword = "";
	opt_pass = false;
	port = 5222;
	opt_host = false;
	host = "";
	opt_automatic_resource = true;
	priority_dep_on_status = true;
	ignore_global_actions = false;
	resource = ApplicationInfo::name();
	priority = 5;
	ibbOnly = false;
	opt_keepAlive = true;
	opt_sm = true;
	allow_plain = XMPP::ClientStream::AllowPlainOverTLS;
	opt_compress = false;
	opt_log = true;
	opt_reconn = false;
	opt_connectAfterSleep = false;
	opt_autoSameStatus = true;
	lastStatusWithPriority = false;
	opt_ignoreSSLWarnings = false;

	proxy_index = 0;
	proxy_type = PROXY_NONE;
	proxy_host = "";
	proxy_port = 8080;
	proxy_user = "";
	proxy_pass = "";

	stunHosts.clear();
	stunHosts << "stun.jabber.ru:5249"
			  << "stun.habahaba.im"
			  << "stun.ekiga.net"
			  << "provserver.televolution.net"
			  << "stun1.voiceeclipse.net"
			  << "stun.callwithus.com"
			  << "stun.counterpath.net"
			  << "stun.endigovoip.com"
			  << "stun.ideasip.com"
			  << "stun.internetcalls.com"
			  << "stun.noc.ams-ix.net"
			  << "stun.phonepower.com"
			  << "stun.phoneserve.com"
			  << "stun.rnktel.com"
			  << "stun.softjoys.com"
			  << "stun.sipgate.net"
			  << "stun.sipgate.net:10000"
			  << "stun.stunprotocol.org"
			  << "stun.voipbuster.com"
			  << "stun.voxgratia.org";

	stunHost = stunHosts[0];

	keybind.clear();

	roster.clear();
}

UserAccount::~UserAccount()
{
}

void UserAccount::fromOptions(OptionsTree *o, QString base)
{
	// WARNING: If you add any new option here, only read the option if
	// allSetOptions (defined below) contains the new option. If not
	// the code should just leave the default value from the reset()
	// call in place.
	optionsBase = base;

	reset();

	QStringList allSetOptions = o->getChildOptionNames(base, true, true);

	opt_enabled = o->getOption(base + ".enabled").toBool();
	opt_auto = o->getOption(base + ".auto").toBool();
	opt_keepAlive = o->getOption(base + ".keep-alive").toBool();
	opt_sm = o->getOption(base + ".enable-sm", true).toBool();
	opt_compress = o->getOption(base + ".compress").toBool();
	req_mutual_auth = o->getOption(base + ".require-mutual-auth").toBool();
	legacy_ssl_probe = o->getOption(base + ".legacy-ssl-probe").toBool();
	opt_automatic_resource = o->getOption(base + ".automatic-resource").toBool();
	priority_dep_on_status = o->getOption(base + ".priority-depends-on-status", false).toBool();
	ignore_global_actions = o->getOption(base + ".ignore-global-actions").toBool();
	opt_log = o->getOption(base + ".log").toBool();
	opt_reconn = o->getOption(base + ".reconn").toBool();
	opt_ignoreSSLWarnings = o->getOption(base + ".ignore-SSL-warnings").toBool();

	// FIX-ME: See FS#771
	if (o->getChildOptionNames().contains(base + ".connect-after-sleep")) {
		opt_connectAfterSleep = o->getOption(base + ".connect-after-sleep").toBool();
	}
	else {
		o->setOption(base + ".connect-after-sleep", opt_connectAfterSleep);
	}

	QString tmpId = o->getOption(base + ".id").toString();
	if (!tmpId.isEmpty()) {
		id = tmpId;
	}
	name = o->getOption(base + ".name").toString();
	jid = o->getOption(base + ".jid").toString();

	customAuth = o->getOption(base + ".custom-auth.use").toBool();
	authid = o->getOption(base + ".custom-auth.authid").toString();
	realm = o->getOption(base + ".custom-auth.realm").toString();

	// read scram salted password options
	storeSaltedHashedPassword = o->getOption(base + ".scram.store-salted-password").toBool();
	scramSaltedHashPassword = o->getOption(base + ".scram.salted-password").toString();

	// read password (we must do this after reading the jid, to decode properly)
	QString tmp = o->getOption(base + ".password").toString();
	if(!tmp.isEmpty()) {
		opt_pass = true;
		pass = decodePassword(tmp, jid);
	}

	opt_host = o->getOption(base + ".use-host").toBool();
	security_level = o->getOption(base + ".security-level").toInt();

	tmp = o->getOption(base + ".ssl").toString();
	if (tmp == "no") {
		ssl = SSL_No;
	} else if (tmp == "yes") {
		ssl = SSL_Yes;
	} else if (tmp == "auto") {
		ssl = SSL_Auto;
	} else if (tmp == "legacy") {
		ssl = SSL_Legacy;
	} else {
		ssl = SSL_Yes;
	}

	host = o->getOption(base + ".host").toString();
	port = o->getOption(base + ".port").toInt();

	resource = o->getOption(base + ".resource").toString();
	priority = o->getOption(base + ".priority").toInt();

	if (allSetOptions.contains(base + ".auto-same-status")) {
		opt_autoSameStatus = o->getOption(base + ".auto-same-status").toBool();
		lastStatus.setType(o->getOption(base + ".last-status").toString());
		lastStatus.setStatus(o->getOption(base + ".last-status-message").toString());
		lastStatusWithPriority = o->getOption(base + ".last-with-priority").toBool();
		if (lastStatusWithPriority) {
			lastStatus.setPriority(o->getOption(base + ".last-priority").toInt());
		}
		else {
			lastStatus.setPriority(defaultPriority(lastStatus));
		}
	}

#ifdef HAVE_PGPUTIL
	QString pgpSecretKeyID = o->getOption(base + ".pgp-secret-key-id").toString();
	if (!pgpSecretKeyID.isEmpty()) {
		QCA::KeyStoreEntry e = PGPUtil::instance().getSecretKeyStoreEntry(pgpSecretKeyID);
		if (!e.isNull())
			pgpSecretKey = e.pgpSecretKey();

		pgpPassPhrase = o->getOption(base + ".pgp-pass-phrase").toString();
		if(!pgpPassPhrase.isEmpty()) {
			pgpPassPhrase = decodePassword(pgpPassPhrase, pgpSecretKeyID);
		}
	}
#endif

	tmp = o->getOption(base + ".allow-plain").toString();
	if (tmp == "never") {
		allow_plain = XMPP::ClientStream::NoAllowPlain;
	} else if (tmp == "always") {
		allow_plain = XMPP::ClientStream::AllowPlain;
	} else if (tmp == "over encryped") {
		allow_plain = XMPP::ClientStream::AllowPlainOverTLS;
	} else {
		allow_plain = XMPP::ClientStream::NoAllowPlain;
	}

	QStringList rosterCache = o->getChildOptionNames(base + ".roster-cache", true, true);
	foreach(QString rbase, rosterCache) {
		RosterItem ri;
		ri.setJid(Jid(o->getOption(rbase + ".jid").toString()));
		ri.setName(o->getOption(rbase + ".name").toString());
		Subscription s;
		s.fromString(o->getOption(rbase + ".subscription").toString());
		ri.setSubscription(s);
		ri.setAsk(o->getOption(rbase + ".ask").toString());
		ri.setGroups(o->getOption(rbase + ".groups").toStringList());
		roster += ri;
	}

	groupState.clear();
	QVariantList states = o->mapKeyList(base + ".group-state");
	foreach(QVariant k, states) {
		GroupData gd;
		QString sbase = o->mapLookup(base + ".group-state", k);
		gd.open = o->getOption(sbase + ".open").toBool();
		gd.rank = o->getOption(sbase + ".rank").toInt();
		groupState.insert(k.toString(), gd);
	}

	proxyID = o->getOption(base + ".proxy-id").toString();

	keybind.fromOptions(o, base + ".pgp-key-bindings");

	dtProxy = o->getOption(base + ".bytestreams-proxy").toString();
	ibbOnly = o->getOption(base + ".ibb-only").toBool();


	if (allSetOptions.contains(base + ".stun-hosts")) {
		stunHosts = o->getOption(base + ".stun-hosts").toStringList();
		if (allSetOptions.contains(base + ".stun-host")) {
			stunHost = o->getOption(base + ".stun-host").toString();
		}
	}
	else if (!o->getOption(base + ".stun-host").toString().isEmpty()) {
		stunHost = o->getOption(base + ".stun-host").toString();
	}
	if (allSetOptions.contains(base + ".stun-username")) {
		stunUser = o->getOption(base + ".stun-username").toString();
	}
	if (allSetOptions.contains(base + ".stun-password")) {
		stunPass = o->getOption(base + ".stun-password").toString();
	}

	if (allSetOptions.contains(base + ".tls")) {
		tlsOverrideCert = o->getOption(base + ".tls.override-certificate").toByteArray();
		tlsOverrideDomain = o->getOption(base + ".tls.override-domain").toString();
	}

	alwaysVisibleContacts = o->getOption(base + ".always-visible-contacts").toStringList();
	localMucBookmarks = o->getOption(base + ".muc-bookmarks").toStringList();
}

void UserAccount::toOptions(OptionsTree *o, QString base)
{
	if (base.isEmpty()) {
		base = optionsBase;
	}
	// clear old data away
	o->removeOption(base, true);

	o->setOption(base + ".enabled", opt_enabled);
	o->setOption(base + ".auto", opt_auto);
	o->setOption(base + ".keep-alive", opt_keepAlive);
	o->setOption(base + ".enable-sm", opt_sm);
	o->setOption(base + ".compress", opt_compress);
	o->setOption(base + ".require-mutual-auth", req_mutual_auth);
	o->setOption(base + ".legacy-ssl-probe", legacy_ssl_probe);
	o->setOption(base + ".automatic-resource", opt_automatic_resource);
	o->setOption(base + ".priority-depends-on-status", priority_dep_on_status);
	o->setOption(base + ".ignore-global-actions", ignore_global_actions);
	o->setOption(base + ".log", opt_log);
	o->setOption(base + ".reconn", opt_reconn);
	o->setOption(base + ".connect-after-sleep", opt_connectAfterSleep);
	o->setOption(base + ".auto-same-status", opt_autoSameStatus);
	o->setOption(base + ".ignore-SSL-warnings", opt_ignoreSSLWarnings);

	o->setOption(base + ".id", id);
	o->setOption(base + ".name", name);
	o->setOption(base + ".jid", jid);

	o->setOption(base + ".custom-auth.use", customAuth);
	o->setOption(base + ".custom-auth.authid", authid);
	o->setOption(base + ".custom-auth.realm", realm);

	o->setOption(base + ".scram.store-salted-password", storeSaltedHashedPassword);
	o->setOption(base + ".scram.salted-password", scramSaltedHashPassword);

	if(opt_pass) {
		o->setOption(base + ".password", encodePassword(pass, jid));
	} else {
		o->setOption(base + ".password", "");
	}
	o->setOption(base + ".use-host", opt_host);
	o->setOption(base + ".security-level", security_level);
	switch (ssl) {
		case SSL_No:
			o->setOption(base + ".ssl", "no");
			break;
		case SSL_Yes:
			o->setOption(base + ".ssl", "yes");
			break;
		case SSL_Auto:
			o->setOption(base + ".ssl", "auto");
			break;
		case SSL_Legacy:
			o->setOption(base + ".ssl", "legacy");
			break;
		default:
			qFatal("unknown ssl enum value in UserAccount::toOptions");
	}
	o->setOption(base + ".host", host);
	o->setOption(base + ".port", port);
	o->setOption(base + ".resource", resource);
	o->setOption(base + ".priority", priority);
	if (!pgpSecretKey.isNull()) {
		o->setOption(base + ".pgp-secret-key-id", pgpSecretKey.keyId());
		o->setOption(base + ".pgp-pass-phrase", encodePassword(pgpPassPhrase, pgpSecretKey.keyId()));
	} else {
		o->setOption(base + ".pgp-secret-key-id", "");
		o->setOption(base + ".pgp-pass-phrase", "");
	}
	switch (allow_plain) {
		case XMPP::ClientStream::NoAllowPlain:
			o->setOption(base + ".allow-plain", "never");
			break;
		case XMPP::ClientStream::AllowPlain:
			o->setOption(base + ".allow-plain", "always");
			break;
		case XMPP::ClientStream::AllowPlainOverTLS:
			o->setOption(base + ".allow-plain", "over encryped");
			break;
		default:
			qFatal("unknown allow_plain enum value in UserAccount::toOptions");
	}

	int idx = 0;
	foreach(RosterItem ri, roster) {
		QString rbase = base + ".roster-cache.a" + QString::number(idx++);
		o->setOption(rbase + ".jid", ri.jid().full());
		o->setOption(rbase + ".name", ri.name());
		o->setOption(rbase + ".subscription", ri.subscription().toString());
		o->setOption(rbase + ".ask", ri.ask());
		o->setOption(rbase + ".groups", ri.groups());
	}

	// now we check for redundant entries
	QStringList groupList;
	QSet<QString> removeList;
	groupList << "/\\/" + name + "\\/\\"; // account name is a very 'special' group

	// special groups that should also have their state remembered
	groupList << qApp->translate("ContactProfile", "General");
	groupList << qApp->translate("ContactProfile", "Agents/Transports");

	// first, add all groups' names to groupList
	foreach(RosterItem i, roster) {
		groupList += i.groups();
	}

	// now, check if there's groupState name entry in groupList
	foreach(QString group, groupState.keys()) {
		if (!groupList.contains(group)) {
			removeList << group;
		}
	}

	// remove redundant groups
	foreach(QString group, removeList) {
		groupState.remove( group );
	}

	// and finally, save the data
	foreach(QString group, groupState.keys()) {
		QString groupBase = o->mapPut(base + ".group-state", group);
		o->setOption(groupBase + ".open", groupState[group].open);
		o->setOption(groupBase + ".rank", groupState[group].rank);
	}

	o->setOption(base + ".proxy-id", proxyID);

	keybind.toOptions(o, base + ".pgp-key-bindings");
	o->setOption(base + ".bytestreams-proxy", dtProxy.full());
	o->setOption(base + ".ibb-only", ibbOnly);

	o->setOption(base + ".stun-hosts", stunHosts);
	o->setOption(base + ".stun-host", stunHost);
	o->setOption(base + ".stun-username", stunUser);
	o->setOption(base + ".stun-password", stunPass);

	o->setOption(base + ".tls.override-certificate", tlsOverrideCert);
	o->setOption(base + ".tls.override-domain", tlsOverrideDomain);
	saveLastStatus(o, base);

	o->setOption(base + ".always-visible-contacts", alwaysVisibleContacts);
	o->setOption(base + ".muc-bookmarks", localMucBookmarks);
}

void UserAccount::fromXml(const QDomElement &a)
{
	reset();

	readEntry(a, "id", &id);
	readEntry(a, "name", &name);
	readBoolAttribute(a, "enabled", &opt_enabled);
	readBoolAttribute(a, "auto", &opt_auto);
	readBoolAttribute(a, "showOffline", &tog_offline);
	readBoolAttribute(a, "showAway", &tog_away);
	readBoolAttribute(a, "showHidden", &tog_hidden);
	readBoolAttribute(a, "showAgents", &tog_agents);
	readBoolAttribute(a, "showSelf", &tog_self);
	readBoolAttribute(a, "keepAlive", &opt_keepAlive);
	readBoolAttribute(a, "enableSM", &opt_sm);
	readBoolAttribute(a, "compress", &opt_compress);
	readBoolAttribute(a, "require-mutual-auth", &req_mutual_auth);
	readBoolAttribute(a, "legacy-ssl-probe", &legacy_ssl_probe);
	readBoolAttribute(a, "log", &opt_log);
	readBoolAttribute(a, "reconn", &opt_reconn);
	readBoolAttribute(a, "ignoreSSLWarnings", &opt_ignoreSSLWarnings);
	//readBoolAttribute(a, "gpg", &opt_gpg);
	readBoolAttribute(a, "automatic-resource", &opt_automatic_resource);
	readBoolAttribute(a, "priority-depends-on-status", &priority_dep_on_status);
	readBoolAttribute(a, "ignore-global-actions", &ignore_global_actions);

	// Will be overwritten if there is a new option
	bool opt_plain = false;
	readBoolAttribute(a, "plain", &opt_plain);
	allow_plain = (opt_plain ? XMPP::ClientStream::AllowPlain : XMPP::ClientStream::NoAllowPlain);
	readNumEntry(a, "allow-plain", (int*) &allow_plain);

	// Will be overwritten if there is a new option
	bool opt_ssl = true;
	readBoolAttribute(a, "ssl", &opt_ssl);
	if (opt_ssl)
		ssl = UserAccount::SSL_Legacy;

	readNumEntry(a, "security-level", &security_level);
	readNumEntry(a, "ssl", (int*) &ssl);
	readEntry(a, "host", &host);
	readNumEntry(a, "port", &port);

	// 0.8.6 and >= 0.9
	QDomElement j = a.firstChildElement("jid");
	if(!j.isNull()) {
		readBoolAttribute(j, "manual", &opt_host);
		jid = tagContent(j);
	}
	// 0.8.7
	else {
		QString user, vhost;
		readEntry(a, "username", &user);
		QDomElement j = a.firstChildElement("vhost");
		if(!j.isNull()) {
			readBoolAttribute(j, "manual", &opt_host);
			vhost = tagContent(j);
		}
		else {
			opt_host = false;
			vhost = host;
			host = "";
			port = 0;
		}
		jid = user + '@' + vhost;
	}

	readBoolEntry(a, "useHost", &opt_host);

	// read password (we must do this after reading the jid, to decode properly)
	readEntry(a, "password", &pass);
	if(!pass.isEmpty()) {
		opt_pass = true;
		pass = decodePassword(pass, jid);
	}

	QDomElement ca = a.firstChildElement("custom-auth");
	if(!ca.isNull()) {
		readBoolAttribute(ca, "use", &customAuth);
		QDomElement authid_el = ca.firstChildElement("authid");
		if (!authid_el.isNull())
			authid = tagContent(authid_el);
		QDomElement realm_el = ca.firstChildElement("realm");
		if (!realm_el.isNull())
			realm = tagContent(realm_el);
	}

	readEntry(a, "resource", &resource);
	readNumEntry(a, "priority", &priority);
	QString pgpSecretKeyID;
	readEntry(a, "pgpSecretKeyID", &pgpSecretKeyID);
#ifdef HAVE_PGPUTIL
	if (!pgpSecretKeyID.isEmpty()) {
		QCA::KeyStoreEntry e = PGPUtil::instance().getSecretKeyStoreEntry(pgpSecretKeyID);
		if (!e.isNull())
			pgpSecretKey = e.pgpSecretKey();

		readEntry(a, "passphrase", &pgpPassPhrase);
		if(!pgpPassPhrase.isEmpty()) {
			pgpPassPhrase = decodePassword(pgpPassPhrase, pgpSecretKeyID);
		}
	}
#endif

	QDomElement r = a.firstChildElement("roster");
	if(!r.isNull()) {
		for(QDomNode n = r.firstChild(); !n.isNull(); n = n.nextSibling()) {
			QDomElement i = n.toElement();
			if(i.isNull())
				continue;

			if(i.tagName() == "item") {
				RosterItem ri;
				if(!ri.fromXml(i))
					continue;
				roster += ri;
			}
		}
	}

	groupState.clear();
	QDomElement gs = a.firstChildElement("groupState");
	if (!gs.isNull()) {
		for (QDomNode n = gs.firstChild(); !n.isNull(); n = n.nextSibling()) {
			QDomElement i = n.toElement();
			if (i.isNull())
				continue;

			if (i.tagName() == "group") {
				GroupData gd;
				gd.open = i.attribute("open") == "true";
				gd.rank = i.attribute("rank").toInt();
				groupState.insert(i.attribute("name"), gd);
			}
		}
	}

	readNumEntry(a, "proxyindex", &proxy_index);
	readNumEntry(a, "proxytype", &proxy_type);
	readEntry(a, "proxyhost", &proxy_host);
	readNumEntry(a, "proxyport", &proxy_port);
	readEntry(a, "proxyuser", &proxy_user);
	readEntry(a, "proxypass", &proxy_pass);
	if(!proxy_pass.isEmpty())
		proxy_pass = decodePassword(proxy_pass, jid);

	r = a.firstChildElement("pgpkeybindings");
	if(!r.isNull())
		keybind.fromXml(r);

	QString str;
	readEntry(a, "dtProxy", &str);
	dtProxy = str;
}

int UserAccount::defaultPriority(const XMPP::Status &s)
{
	if (priority_dep_on_status) {
		if (s.isAvailable()) {
			return PsiOptions::instance()->getOption("options.status.default-priority." + s.typeString()).toInt();
		}
		else {
			return 0; //Priority for Offline status, it is not used
		}
	}
	else {
		return priority;
	}
}

void UserAccount::saveLastStatus(OptionsTree *o, QString base=QString())
{
	if (base.isEmpty()) {
		base = optionsBase;
	}

	o->setOption(base + ".last-status", lastStatus.typeString());
	o->setOption(base + ".last-status-message", lastStatus.status());
	o->setOption(base + ".last-with-priority", lastStatusWithPriority);
	if (lastStatusWithPriority) {
		o->setOption(base + ".last-priority", lastStatus.priority());
	}
	else {
		o->removeOption(base + ".last-priority");
	}
}

static ToolbarPrefs loadToolbarData( const QDomElement &e )
{
	QDomElement tb_prefs = e;
	ToolbarPrefs tb;

	readEntry(tb_prefs, "name",		&tb.name);
	readBoolEntry(tb_prefs, "on",		&tb.on);
	readBoolEntry(tb_prefs, "locked",	&tb.locked);
	// readBoolEntry(tb_prefs, "stretchable",	&tb.stretchable);
	xmlToStringList(tb_prefs, "keys",	&tb.keys);

	QDomElement tb_position = tb_prefs.firstChildElement("position");
	if (!tb_position.isNull())
	{
		QString dockStr;
		Qt3Dock dock = Qt3Dock_Top;
		readEntry(tb_position, "dock", &dockStr);
		if (dockStr == "DockTop")
			dock = Qt3Dock_Top;
		else if (dockStr == "DockBottom")
			dock = Qt3Dock_Bottom;
		else if (dockStr == "DockLeft")
			dock = Qt3Dock_Left;
		else if (dockStr == "DockRight")
			dock = Qt3Dock_Right;
		else if (dockStr == "DockMinimized")
			dock = Qt3Dock_Minimized;
		else if (dockStr == "DockTornOff")
			dock = Qt3Dock_TornOff;
		else if (dockStr == "DockUnmanaged")
			dock = Qt3Dock_Unmanaged;

		tb.dock = dock;

		// readNumEntry(tb_position, "index",		&tb.index);
		readBoolEntry(tb_position, "nl",		&tb.nl);
		// readNumEntry(tb_position, "extraOffset",	&tb.extraOffset);
	}

	return tb;
}


bool OptionsMigration::fromFile(const QString &fname)
{
	QString confver;
	QDomDocument doc;
	QString progver;

	AtomicXmlFile f(fname);
	if (!f.loadDocument(&doc))
		return false;

	QDomElement base = doc.documentElement();
	if(base.tagName() != "psiconf")
		return false;
	confver = base.attribute("version");
	if(confver != "1.0")
		return false;

	readEntry(base, "progver", &progver);

	// migrateRectEntry(base, "geom", "options.ui.contactlist.saved-window-geometry");
	migrateStringList(base, "recentGCList", "options.muc.recent-joins.jids");
	migrateStringList(base, "recentBrowseList", "options.ui.service-discovery.recent-jids");
	migrateStringEntry(base, "lastStatusString", "options.status.last-message");
	migrateBoolEntry(base, "useSound", "options.ui.notifications.sounds.enable");

	QDomElement accs = base.firstChildElement("accounts");
	if(!accs.isNull()) {
		for(QDomNode n = accs.firstChild(); !n.isNull(); n = n.nextSibling()) {
			QDomElement a = n.toElement();
			if(a.isNull())
				continue;

			if(a.tagName() == "account") {
				UserAccount ua;
				ua.fromXml(a);
				accMigration.append(ua);
			}
		}
	}

	// convert old proxy config into new
	for(UserAccountList::Iterator it = accMigration.begin(); it != accMigration.end(); ++it) {
		UserAccount &a = *it;
		if(a.proxy_type > 0) {
			ProxyItem p;
			p.name = QObject::tr("%1 Proxy").arg(a.name);
			p.type = "http";
			p.settings.host = a.proxy_host;
			p.settings.port = a.proxy_port;
			p.settings.useAuth = !a.proxy_user.isEmpty();
			p.settings.user = a.proxy_user;
			p.settings.pass = a.proxy_pass;
			proxyMigration.append(p);

			a.proxy_index = proxyMigration.count(); // 1 and up are proxies
		}
	}

	QDomElement prox = base.firstChildElement("proxies");
	if(!prox.isNull()) {
		QDomNodeList list = prox.elementsByTagName("proxy");
		for(int n = 0; n < list.count(); ++n) {
			QDomElement e = list.item(n).toElement();
			ProxyItem p;
			p.name = "";
			p.type = "";
			readEntry(e, "name", &p.name);
			readEntry(e, "type", &p.type);
			if(p.type == "0")
				p.type = "http";
			QDomElement pset = e.elementsByTagName("proxySettings").item(0).toElement();
			if(!pset.isNull())
				p.settings.fromXml(pset);
			proxyMigration.append(p);
		}
	}

	// assign storage IDs to proxies and update accounts
	for (int i=0; i < proxyMigration.size(); i++) {
		proxyMigration[i].id = "a"+QString::number(i);
	}
	for (int i=0; i < accMigration.size(); i++) {
		if (accMigration[i].proxy_index != 0) {
			accMigration[i].proxyID = proxyMigration[accMigration[i].proxy_index-1].id;
		}
	}



	PsiOptions::instance()->setOption("options.ui.contactlist.show.offline-contacts", true);
	PsiOptions::instance()->setOption("options.ui.contactlist.show.away-contacts", true);
	PsiOptions::instance()->setOption("options.ui.contactlist.show.hidden-contacts-group", true);
	PsiOptions::instance()->setOption("options.ui.contactlist.show.agent-contacts", true);
	PsiOptions::instance()->setOption("options.ui.contactlist.show.self-contact", true);

	for (int i=0; i < accMigration.size(); i++) {
		if (!accMigration[i].opt_enabled) continue;
		PsiOptions::instance()->setOption("options.ui.contactlist.show.offline-contacts", accMigration[i].tog_offline);
		PsiOptions::instance()->setOption("options.ui.contactlist.show.away-contacts", accMigration[i].tog_away);
		PsiOptions::instance()->setOption("options.ui.contactlist.show.hidden-contacts-group", accMigration[i].tog_hidden);
		PsiOptions::instance()->setOption("options.ui.contactlist.show.agent-contacts", accMigration[i].tog_agents);
		PsiOptions::instance()->setOption("options.ui.contactlist.show.self-contact", accMigration[i].tog_self);
		break;
	}


	QDomElement p = base.firstChildElement("preferences");
	if(!p.isNull()) {
		QDomElement p_general = p.firstChildElement("general");
		if(!p_general.isNull()) {
			QDomElement p_roster = p_general.firstChildElement("roster");
			if(!p_roster.isNull()) {
				migrateBoolEntry(p_roster, "useleft", "options.ui.contactlist.use-left-click");
				migrateBoolEntry(p_roster, "singleclick", "options.ui.contactlist.use-single-click");
				bool hideMenu;
				readBoolEntry(p_roster, "hideMenubar", &hideMenu);
				PsiOptions::instance()->setOption("options.ui.contactlist.show-menubar", !hideMenu);
				int defaultAction;
				readNumEntry(p_roster, "defaultAction", &defaultAction);
				PsiOptions::instance()->setOption("options.messages.default-outgoing-message-type", defaultAction == 0 ? "message" : "chat");
				migrateBoolEntry(p_roster, "useTransportIconsForContacts", "options.ui.contactlist.use-transport-icons");

				QDomElement sorting = p_roster.firstChildElement("sortStyle");
				if(!sorting.isNull()) {
					QString name;

					migrateStringEntry(sorting, "contact", "options.ui.contactlist.contact-sort-style");
					migrateStringEntry(sorting, "group", "options.ui.contactlist.group-sort-style");
					migrateStringEntry(sorting, "account", "options.ui.contactlist.account-sort-style");

					/* FIXME
					readEntry(sorting, "contact", &name);
					if ( name == "alpha" )
						lateMigrationData.rosterContactSortStyle = Options::ContactSortStyle_Alpha;
					else
						lateMigrationData.rosterContactSortStyle = Options::ContactSortStyle_Status;

					readEntry(sorting, "group", &name);
					if ( name == "rank" )
						lateMigrationData.rosterGroupSortStyle = Options::GroupSortStyle_Rank;
					else
						lateMigrationData.rosterGroupSortStyle = Options::GroupSortStyle_Alpha;

					readEntry(sorting, "account", &name);
					if ( name == "rank" )
						lateMigrationData.rosterAccountSortStyle = Options::AccountSortStyle_Rank;
					else
						lateMigrationData.rosterAccountSortStyle = Options::AccountSortStyle_Alpha;
					*/
				}
			}

			QDomElement tag = p_general.firstChildElement("misc");
			if(!tag.isNull()) {
				int delafterint;
				readNumEntry(tag, "delChats", &delafterint);
				QString delafter;
				switch (delafterint) {
					case 0:
						delafter = "instant";
						break;
					case 1:
						delafter = "hour";
						break;
					case 2:
						delafter = "day";
						break;
					case 3:
						delafter = "never";
						break;
				}
				PsiOptions::instance()->setOption("options.ui.chat.delete-contents-after", delafter);
				migrateBoolEntry(tag, "alwaysOnTop", "options.ui.contactlist.always-on-top");
				migrateBoolEntry(tag, "ignoreHeadline", "options.messages.ignore-headlines");
				migrateBoolEntry(tag, "ignoreNonRoster", "options.messages.ignore-non-roster-contacts");
				migrateBoolEntry(tag, "excludeGroupChatIgnore", "options.messages.exclude-muc-from-ignore");
				migrateBoolEntry(tag, "scrollTo", "options.ui.contactlist.ensure-contact-visible-on-event");
				migrateBoolEntry(tag, "useEmoticons", "options.ui.emoticons.use-emoticons");
				migrateBoolEntry(tag, "alertOpenChats", "options.ui.chat.alert-for-already-open-chats");
				migrateBoolEntry(tag, "raiseChatWindow", "options.ui.chat.raise-chat-windows-on-new-messages");
				migrateBoolEntry(tag, "showSubjects", "options.ui.message.show-subjects");
				migrateBoolEntry(tag, "showGroupCounts", "options.ui.contactlist.show-group-counts");
				migrateBoolEntry(tag, "showCounter", "options.ui.message.show-character-count");
				migrateBoolEntry(tag, "chatSays", "options.ui.chat.use-chat-says-style");
				migrateBoolEntry(tag, "jidComplete", "options.ui.message.use-jid-auto-completion");
				migrateBoolEntry(tag, "grabUrls", "options.ui.message.auto-grab-urls-from-clipboard");
				migrateBoolEntry(tag, "smallChats", "options.ui.chat.use-small-chats");
				migrateBoolEntry(tag, "chatLineEdit", "options.ui.chat.use-expanding-line-edit");
				migrateBoolEntry(tag, "useTabs", "options.ui.tabs.use-tabs");
				migrateBoolEntry(tag, "putTabsAtBottom", "options.ui.tabs.put-tabs-at-bottom");
				migrateBoolEntry(tag, "autoRosterSize", "options.ui.contactlist.automatically-resize-roster");
				migrateBoolEntry(tag, "autoRosterSizeGrowTop", "options.ui.contactlist.grow-roster-upwards");
				migrateBoolEntry(tag, "autoResolveNicksOnAdd", "options.contactlist.resolve-nicks-on-contact-add");
				migrateBoolEntry(tag, "messageEvents", "options.messages.send-composing-events");
				migrateBoolEntry(tag, "inactiveEvents", "options.messages.send-inactivity-events");
				migrateStringEntry(tag, "lastPath", "options.ui.last-used-open-path");
				migrateStringEntry(tag, "lastSavePath", "options.ui.last-used-save-path");
				migrateBoolEntry(tag, "autoCopy", "options.ui.automatically-copy-selected-text");
				migrateBoolEntry(tag, "useCaps", "options.service-discovery.enable-entity-capabilities");
				migrateBoolEntry(tag, "rc", "options.external-control.adhoc-remote-control.enable");

				// Migrating for soft return option
				tag.firstChildElement("chatSoftReturn");
				if (!tag.isNull()) {
					bool soft;
					readBoolEntry(tag, "chatSoftReturn", &soft);
					QVariantList vl;
					if (soft)
						vl << qVariantFromValue(QKeySequence(Qt::Key_Enter)) << qVariantFromValue(QKeySequence(Qt::Key_Return));
					else
						vl << qVariantFromValue(QKeySequence(Qt::Key_Enter+Qt::CTRL)) << qVariantFromValue(QKeySequence(Qt::CTRL+Qt::Key_Return));
					PsiOptions::instance()->setOption("options.shortcuts.chat.send",vl);
				}
			}

			tag = p_general.firstChildElement("dock");
			if(!tag.isNull()) {
				migrateBoolEntry(tag, "useDock", "options.ui.systemtray.enable");
				migrateBoolEntry(tag, "dockDCstyle", "options.ui.systemtray.use-double-click");
				migrateBoolEntry(tag, "dockHideMW", "options.contactlist.hide-on-start");
				migrateBoolEntry(tag, "dockToolMW", "options.contactlist.use-toolwindow");
			}

			/*tag = p_general.firstChildElement("security");
			if(!tag.isNull()) {
				readEntry(tag, "pgp", &prefs.pgp);
			}*/
		}


		QDomElement p_events = p.firstChildElement("events");
		if(!p_events.isNull()) {

			int alertstyle;
			readNumEntry(p_events, "alertstyle", &alertstyle);
			QString ase[3] = {"no", "blink", "animate"};
			PsiOptions::instance()->setOption("options.ui.notifications.alert-style", ase[alertstyle]);
			migrateBoolEntry(p_events, "autoAuth", "options.subscriptions.automatically-allow-authorization");
			migrateBoolEntry(p_events, "notifyAuth", "options.ui.notifications.successful-subscription");

			QDomElement tag = p_events.firstChildElement("receive");
			if(!tag.isNull()) {
				migrateBoolEntry(tag, "popupMsgs", "options.ui.message.auto-popup");
				migrateBoolEntry(tag, "popupChats", "options.ui.chat.auto-popup");
				migrateBoolEntry(tag, "popupHeadlines", "options.ui.message.auto-popup-headlines");
				migrateBoolEntry(tag, "popupFiles", "options.ui.file-transfer.auto-popup");
				migrateBoolEntry(tag, "noAwayPopup", "options.ui.notifications.popup-dialogs.suppress-while-away");
				migrateBoolEntry(tag, "noUnlistedPopup", "options.ui.notifications.popup-dialogs.suppress-when-not-on-roster");
				migrateBoolEntry(tag, "raise", "options.ui.contactlist.raise-on-new-event");
				int force = 0;
				readNumEntry(tag, "incomingAs", &force);
				QString fe[4] = {"no", "message", "chat", "current-open"};
				PsiOptions::instance()->setOption("options.messages.force-incoming-message-type", fe[force]);
			}

		}

		QDomElement p_pres = p.firstChildElement("presence");
		if(!p_pres.isNull()) {

			QDomElement tag = p_pres.firstChildElement("misc");
			if(!tag.isNull()) {
				migrateBoolEntry(tag, "askOnline", "options.status.ask-for-message-on-online");
				migrateBoolEntry(tag, "askOffline", "options.status.ask-for-message-on-offline");
				migrateBoolEntry(tag, "rosterAnim", "options.ui.contactlist.use-status-change-animation");
				migrateBoolEntry(tag, "autoVCardOnLogin", "options.vcard.query-own-vcard-on-login");
				migrateBoolEntry(tag, "xmlConsoleOnLogin", "options.xml-console.enable-at-login");
			}

			tag = p_pres.firstChildElement("autostatus");
			if(!tag.isNull()) {
				bool use;
				QDomElement e;
				e = tag.firstChildElement("away");
				if(!e.isNull()) {
					if(e.hasAttribute("use")) {
						readBoolAttribute(e, "use", &use);
						PsiOptions::instance()->setOption("options.status.auto-away.use-away", use);
					}
				}
				e = tag.firstChildElement("xa");
				if(!e.isNull()) {
					if(e.hasAttribute("use"))
						readBoolAttribute(e, "use", &use);
					PsiOptions::instance()->setOption("options.status.auto-away.use-not-availible", use);
				}
				e = tag.firstChildElement("offline");
				if(!e.isNull()) {
					if(e.hasAttribute("use"))
						readBoolAttribute(e, "use", &use);
					PsiOptions::instance()->setOption("options.status.auto-away.use-offline", use);
				}

				migrateIntEntry(tag, "away", "options.status.auto-away.away-after");
				migrateIntEntry(tag, "xa", "options.status.auto-away.not-availible-after");
				migrateIntEntry(tag, "offline", "options.status.auto-away.offline-after");

				migrateStringEntry(tag, "message", "options.status.auto-away.message");
			}

			tag = p_pres.firstChildElement("statuspresets");
			if(!tag.isNull()) {
				lateMigrationData.sp.clear();
				for(QDomNode n = tag.firstChild(); !n.isNull(); n = n.nextSibling()) {
					StatusPreset preset(n.toElement());
					if (!preset.name().isEmpty())
						lateMigrationData.sp[preset.name()] = preset;
				}
			}
		}

		QDomElement p_lnf = p.firstChildElement("lookandfeel");
		if(!p_lnf.isNull()) {

			migrateBoolEntry(p_lnf, "newHeadings", "options.ui.look.contactlist.use-slim-group-headings");
			migrateBoolEntry(p_lnf, "outline-headings", "options.ui.look.contactlist.use-outlined-group-headings");
			migrateIntEntry(p_lnf, "chat-opacity", "options.ui.chat.opacity");
			migrateIntEntry(p_lnf, "roster-opacity", "options.ui.contactlist.opacity");

			QDomElement tag = p_lnf.firstChildElement("colors");
			if(!tag.isNull()) {
				migrateColorEntry(tag, "online", "options.ui.look.colors.contactlist.status.online");
				migrateColorEntry(tag, "listback", "options.ui.look.colors.contactlist.background");
				migrateColorEntry(tag, "away", "options.ui.look.colors.contactlist.status.away");
				migrateColorEntry(tag, "dnd", "options.ui.look.colors.contactlist.status.do-not-disturb");
				migrateColorEntry(tag, "offline", "options.ui.look.colors.contactlist.status.offline");
				migrateColorEntry(tag, "status", "options.ui.look.colors.contactlist.status-messages");
				migrateColorEntry(tag, "groupfore", "options.ui.look.colors.contactlist.grouping.header-foreground");
				migrateColorEntry(tag, "groupback", "options.ui.look.colors.contactlist.grouping.header-background");
				migrateColorEntry(tag, "profilefore", "options.ui.look.colors.contactlist.profile.header-foreground");
				migrateColorEntry(tag, "profileback", "options.ui.look.colors.contactlist.profile.header-background");
				migrateColorEntry(tag, "animfront", "options.ui.look.colors.contactlist.status-change-animation1");
				migrateColorEntry(tag, "animback", "options.ui.look.colors.contactlist.status-change-animation2");
			}

			tag = p_lnf.firstChildElement("fonts");
			if(!tag.isNull()) {
				migrateStringEntry(tag, "roster", "options.ui.look.font.contactlist");
				migrateStringEntry(tag, "message", "options.ui.look.font.message");
				migrateStringEntry(tag, "chat", "options.ui.look.font.chat");
				migrateStringEntry(tag, "popup", "options.ui.look.font.passive-popup");
			}
		}

		QDomElement p_sound = p.firstChildElement("sound");
		if(!p_sound.isNull()) {

			QString oldplayer;
			readEntry(p_sound, "player", &oldplayer);
			// psi now auto detects "play" or "aplay"
			// force auto detection on for old default and simple case of aplay on
			// alsa enabled systems.
			if (oldplayer != soundDetectPlayer() && oldplayer != "play") {
				PsiOptions::instance()->setOption("options.ui.notifications.sounds.unix-sound-player", oldplayer);
			} else {
				PsiOptions::instance()->setOption("options.ui.notifications.sounds.unix-sound-player", "");
			}
			migrateBoolEntry(p_sound, "noawaysound", "options.ui.notifications.sounds.silent-while-away");
			bool noGCSound;
			readBoolEntry(p_sound, "noGCSound", &noGCSound);
			PsiOptions::instance()->setOption("options.ui.notifications.sounds.notify-every-muc-message", !noGCSound);

			QDomElement tag = p_sound.firstChildElement("onevent");
			if(!tag.isNull()) {
				migrateStringEntry(tag, "message", "options.ui.notifications.sounds.incoming-message");
				migrateStringEntry(tag, "chat1", "options.ui.notifications.sounds.new-chat");
				migrateStringEntry(tag, "chat2", "options.ui.notifications.sounds.chat-message");
				migrateStringEntry(tag, "system", "options.ui.notifications.sounds.system-message");
				migrateStringEntry(tag, "headline", "options.ui.notifications.sounds.incoming-headline");
				migrateStringEntry(tag, "online", "options.ui.notifications.sounds.contact-online");
				migrateStringEntry(tag, "offline", "options.ui.notifications.sounds.contact-offline");
				migrateStringEntry(tag, "send", "options.ui.notifications.sounds.outgoing-chat");
				migrateStringEntry(tag, "incoming_ft", "options.ui.notifications.sounds.incoming-file-transfer");
				migrateStringEntry(tag, "ft_complete", "options.ui.notifications.sounds.completed-file-transfer");
			}
		}

		QDomElement p_sizes = p.firstChildElement("sizes");
		if(!p_sizes.isNull()) {
			migrateSizeEntry(p_sizes, "eventdlg", "options.ui.message.size");
			migrateSizeEntry(p_sizes, "chatdlg", "options.ui.chat.size");
			migrateSizeEntry(p_sizes, "tabdlg", "options.ui.tabs.size");
		}

		QDomElement p_toolbars = p.firstChildElement("toolbars");
		if (!p_toolbars.isNull()) {
			QStringList goodTags;
			goodTags << "toolbar";
			goodTags << "mainWin";

			bool mainWinCleared = false;
			bool oldStyle = true;

			for(QDomNode n = p_toolbars.firstChild(); !n.isNull(); n = n.nextSibling()) {
				QDomElement e = n.toElement();
				if( e.isNull() )
					continue;

				QString tbGroup;
				bool isGood = false;
				QStringList::Iterator it = goodTags.begin();
				for ( ; it != goodTags.end(); ++it ) {
					if ( e.tagName().left( (*it).length() ) == *it ) {
						isGood = true;

						if ( e.tagName().left(7) == "toolbar" )
							tbGroup = "mainWin";
						else {
							tbGroup = *it;
							oldStyle = false;
						}

						break;
					}
				}

				if ( isGood ) {
					if ( tbGroup != "mainWin" || !mainWinCleared ) {
						lateMigrationData.toolbars[tbGroup].clear();
						if ( tbGroup == "mainWin" )
							mainWinCleared = true;
					}

					if ( oldStyle ) {
						ToolbarPrefs tb = loadToolbarData( e );
						lateMigrationData.toolbars[tbGroup].append(tb);
					}
					else {
						for(QDomNode nn = e.firstChild(); !nn.isNull(); nn = nn.nextSibling()) {
							QDomElement ee = nn.toElement();
							if( ee.isNull() )
								continue;

							if ( ee.tagName() == "toolbar" ) {
								ToolbarPrefs tb = loadToolbarData( ee );
								lateMigrationData.toolbars[tbGroup].append(tb);
							}
						}
					}
				}
			}

			// event notifier in these versions was not implemented as an action, so add it
			if ( progver == "0.9" || progver == "0.9-CVS" ) {
				// at first, we need to scan the options, to determine, whether event_notifier already available
				bool found = false;
				QList<ToolbarPrefs>::Iterator it = lateMigrationData.toolbars["mainWin"].begin();
				for ( ; it != lateMigrationData.toolbars["mainWin"].end(); ++it) {
					QStringList::Iterator it2 = (*it).keys.begin();
					for ( ; it2 != (*it).keys.end(); ++it2) {
						if ( *it2 == "event_notifier" ) {
							found = true;
							break;
						}
					}
				}

				if ( !found ) {
					ToolbarPrefs tb;
					tb.name = QObject::tr("Event notifier");
					tb.on = false;
					tb.locked = true;
					// tb.stretchable = true;
					tb.keys << "event_notifier";
					tb.dock  = Qt3Dock_Bottom;
					// tb.index = 0;
					lateMigrationData.toolbars["mainWin"].append(tb);
				}
			}
		}

		//group chat
		QDomElement p_groupchat = p.firstChildElement("groupchat");
		if (!p_groupchat.isNull()) {
			migrateBoolEntry(p_groupchat, "nickcoloring", "options.ui.muc.use-nick-coloring");
			migrateBoolEntry(p_groupchat, "highlighting", "options.ui.muc.use-highlighting");
			migrateStringList(p_groupchat, "highlightwords", "options.ui.muc.highlight-words");
			migrateStringList(p_groupchat, "nickcolors", "options.ui.look.colors.muc.nick-colors");
		}

		// Bouncing dock icon (Mac OS X)
		QDomElement p_dock = p.firstChildElement("dock");
		if(!p_dock.isNull()) {
			PsiOptions::instance()->setOption("options.ui.notifications.bounce-dock", p_dock.attribute("bounce"));
			/* FIXME convert back to some modern enum?
			if (p_dock.attribute("bounce") == "once") {
				lateMigrationData.bounceDock = Options::BounceOnce;
			}
			else if (p_dock.attribute("bounce") == "forever") {
				lateMigrationData.bounceDock = Options::BounceForever;
			}
			else if (p_dock.attribute("bounce") == "never") {
				lateMigrationData.bounceDock = Options::NoBounce;
			}*/
		}

		QDomElement p_popup = p.firstChildElement("popups");
		if(!p_popup.isNull()) {
			migrateBoolEntry(p_popup, "on", "options.ui.notifications.passive-popups.enabled");
			migrateBoolEntry(p_popup, "online", "options.ui.notifications.passive-popups.status.online");
			migrateBoolEntry(p_popup, "offline", "options.ui.notifications.passive-popups.status.offline");
			migrateBoolEntry(p_popup, "statusChange", "options.ui.notifications.passive-popups.status.other-changes");
			migrateBoolEntry(p_popup, "message", "options.ui.notifications.passive-popups.incoming-message");
			migrateBoolEntry(p_popup, "chat", "options.ui.notifications.passive-popups.incoming-chat");
			migrateBoolEntry(p_popup, "headline", "options.ui.notifications.passive-popups.incoming-headline");
			migrateBoolEntry(p_popup, "file", "options.ui.notifications.passive-popups.incoming-file-transfer");
			migrateIntEntry(p_popup,  "jidClip", "options.ui.notifications.passive-popups.maximum-jid-length");
			migrateIntEntry(p_popup,  "statusClip", "options.ui.notifications.passive-popups.maximum-status-length");
			migrateIntEntry(p_popup,  "textClip", "options.ui.notifications.passive-popups.maximum-text-length");
			migrateIntEntry(p_popup,  "hideTime", "options.ui.notifications.passive-popups.duration");
			migrateColorEntry(p_popup, "borderColor", "options.ui.look.colors.passive-popup.border");
		}

		QDomElement p_lockdown = p.firstChildElement("lockdown");
		if(!p_lockdown.isNull()) {
			migrateBoolEntry(p_lockdown, "roster", "options.ui.contactlist.lockdown-roster");
			migrateBoolEntry(p_lockdown, "services", "options.ui.contactlist.disable-service-discovery");
		}

		QDomElement p_iconset = p.firstChildElement("iconset");
		if(!p_iconset.isNull()) {
			migrateStringEntry(p_iconset, "system", "options.iconsets.system");

			QDomElement roster = p_iconset.firstChildElement("roster");
			if (!roster.isNull()) {
				migrateStringEntry(roster, "default", "options.iconsets.status");

				QDomElement service = roster.firstChildElement("service");
				if (!service.isNull()) {
					lateMigrationData.serviceRosterIconset.clear();
					for (QDomNode n = service.firstChild(); !n.isNull(); n = n.nextSibling()) {
						QDomElement i = n.toElement();
						if ( i.isNull() )
							continue;

						lateMigrationData.serviceRosterIconset[i.attribute("service")] = i.attribute("iconset");
					}
				}

				QDomElement custom = roster.firstChildElement("custom");
				if (!custom.isNull()) {
					lateMigrationData.customRosterIconset.clear();
					for (QDomNode n = custom.firstChild(); !n.isNull(); n = n.nextSibling()) {
						QDomElement i = n.toElement();
						if ( i.isNull() )
							continue;

						lateMigrationData.customRosterIconset[i.attribute("regExp")] = i.attribute("iconset");
					}
				}
			}

			QDomElement emoticons = p_iconset.firstChildElement("emoticons");
			if (!emoticons.isNull()) {
				QStringList emoticons_list;
				for (QDomNode n = emoticons.firstChild(); !n.isNull(); n = n.nextSibling()) {
					QDomElement i = n.toElement();
					if ( i.isNull() )
						continue;

					if ( i.tagName() == "item" ) {
						QString is = i.text();
						emoticons_list << is;
					}
				}
				PsiOptions::instance()->setOption("options.iconsets.emoticons", emoticons_list);
			}
		}

		QDomElement p_tip = p.firstChildElement("tipOfTheDay");
		if (!p_tip.isNull()) {
			migrateIntEntry(p_tip, "num", "options.ui.tip.number");
			migrateBoolEntry(p_tip, "show", "options.ui.tip.show");
		}

		QDomElement p_disco = p.firstChildElement("disco");
		if (!p_disco.isNull()) {
			migrateBoolEntry(p_disco, "items", "options.ui.service-discovery.automatically-get-items");
			migrateBoolEntry(p_disco, "info", "options.ui.service-discovery.automatically-get-info");
		}

		QDomElement p_dt = p.firstChildElement("dt");
		if (!p_dt.isNull()) {
			migrateIntEntry(p_dt, "port", "options.p2p.bytestreams.listen-port");
			migrateStringEntry(p_dt, "external", "options.p2p.bytestreams.external-address");
		}

		QDomElement p_globalAccel = p.firstChildElement("globalAccel");
		if (!p_globalAccel.isNull()) {
			for (QDomNode n = p_globalAccel.firstChild(); !n.isNull(); n = n.nextSibling()) {
				QDomElement i = n.toElement();
				if ( i.isNull() )
					continue;

				if ( i.tagName() == "command" && i.hasAttribute("type") ) {
					QVariant k = qVariantFromValue(QKeySequence(i.text()));
					QString shortcut;
					if (i.attribute("type") == "processNextEvent")
						shortcut = "event";
					else
						shortcut = "toggle-visibility";
					PsiOptions::instance()->setOption(QString("options.shortcuts.global.") + shortcut, k);
				}
			}
		}

		QDomElement p_advWidget = p.firstChildElement("advancedWidget");
		if (!p_advWidget.isNull()) {
			QDomElement stick = p_advWidget.firstChildElement("sticky");
			if (!stick.isNull()) {
				bool enabled, toWindows;
				int  offs;

				readBoolAttribute(stick, "enabled", &enabled);
				readNumEntry(stick, "offset", &offs);
				readBoolEntry(stick, "stickToWindows", &toWindows);

				GAdvancedWidget::setStickEnabled( enabled );
				GAdvancedWidget::setStickAt( offs );
				GAdvancedWidget::setStickToWindows( toWindows );
			}
		}
	}

	return true;
}

void OptionsMigration::lateMigration()
{
	// Add default chat and groupchat toolbars
	if (PsiOptions::instance()->getOption("options.ui.contactlist.toolbars.m0.name").toString() != "Chat") {
		QStringList pluginsKeys;
#ifdef PSI_PLUGINS
		PluginManager *pm = PluginManager::instance();
		QStringList plugins = pm->availablePlugins();
		foreach (const QString &plugin, plugins) {
			pluginsKeys << pm->shortName(plugin) + "-plugin";
		}
#endif
		ToolbarPrefs chatToolbar;
		chatToolbar.on = PsiOptions::instance()->getOption("options.ui.chat.central-toolbar").toBool();
		PsiOptions::instance()->removeOption("options.ui.chat.central-toolbar");
		chatToolbar.name = "Chat";
		chatToolbar.keys << "chat_clear"  << "chat_find" << "chat_html_text" << "chat_add_contact";
		chatToolbar.keys += pluginsKeys;
		chatToolbar.keys << "spacer" << "chat_icon" << "chat_file"
						 << "chat_pgp" << "chat_info" << "chat_history" << "chat_voice"
						 << "chat_active_contacts";

		if (PsiOptions::instance()->getOption("options.ui.chat.disable-paste-send").toBool()) {
			chatToolbar.keys.removeAt(chatToolbar.keys.indexOf("chat_ps"));
		}

		ToolbarPrefs groupchatToolbar;
		groupchatToolbar.on = chatToolbar.on;

		groupchatToolbar.name = "Groupchat";
		groupchatToolbar.keys << "gchat_clear"  << "gchat_find" << "gchat_html_text" << "gchat_configure";
		groupchatToolbar.keys += pluginsKeys;
		groupchatToolbar.keys << "spacer" << "gchat_icon" ;

		if (PsiOptions::instance()->getOption("options.ui.chat.disable-paste-send").toBool()) {
			groupchatToolbar.keys.removeAt(groupchatToolbar.keys.indexOf("gchat_ps"));
		}
		PsiOptions::instance()->removeOption("options.ui.chat.disable-paste-send");

		QList<ToolbarPrefs> toolbars;
		toolbars << chatToolbar
		         << groupchatToolbar;

		QStringList toolbarBases = PsiOptions::instance()->getChildOptionNames("options.ui.contactlist.toolbars", true, true);
		foreach(QString base, toolbarBases) {
			ToolbarPrefs tb;
			tb.id = PsiOptions::instance()->getOption(base + ".key").toString();
			tb.name = PsiOptions::instance()->getOption(base + ".name").toString();
			if (tb.id.isEmpty() || tb.name.isEmpty()) {
				qDebug("Does not look like a toolbar");
				continue;
			}

			tb.on = PsiOptions::instance()->getOption(base + ".visible").toBool();
			tb.locked = PsiOptions::instance()->getOption(base + ".locked").toBool();
			tb.dock = (Qt3Dock)PsiOptions::instance()->getOption(base + ".dock.position").toInt(); //FIXME
			tb.nl = PsiOptions::instance()->getOption(base + ".dock.nl").toBool();
			tb.keys = PsiOptions::instance()->getOption(base + ".actions").toStringList();

			toolbars << tb;
		}

		PsiOptions::instance()->removeOption("options.ui.contactlist.toolbars", true);

		foreach(ToolbarPrefs tb, toolbars) {
			tb.locked = true;
			PsiToolBar::structToOptions(PsiOptions::instance(), tb);
		}
	}

	foreach(QString opt, PsiOptions::instance()->allOptionNames()) {
		if (opt.startsWith("options.status.presets.") ||
			opt.startsWith("options.iconsets.service-status.") ||
			opt.startsWith("options.iconsets.custom-status."))
		{
			return;
		}
	}

	PsiOptions *o = PsiOptions::instance();
	// QMap<QString, QString> serviceRosterIconset;
	QMapIterator<QString, QString> iSRI(lateMigrationData.serviceRosterIconset);
	while (iSRI.hasNext()) {
		iSRI.next();
		QString base = o->mapPut("options.iconsets.service-status", iSRI.key());
		o->setOption(base + ".iconset", iSRI.value());
	}

	// QMap<QString, QString> customRosterIconset;
	int idx = 0;
	QMapIterator<QString, QString> iCRI(lateMigrationData.customRosterIconset);
	while (iCRI.hasNext()) {
		iCRI.next();
		QString base = "options.iconsets.custom-status" ".a" + QString::number(idx++);
		o->setOption(base + ".regexp", iCRI.key());
		o->setOption(base + ".iconset", iCRI.value());
	}

	// QMap<QString,StatusPreset> sp; // Status message presets.
	foreach(StatusPreset sp, lateMigrationData.sp) {
		sp.toOptions(o);
	}

	// QMap< QString, QList<ToolbarPrefs> > toolbars;
	QList<ToolbarPrefs> toolbars;
	if(qVersionInt() >= 0x040300) {
		toolbars = lateMigrationData.toolbars["mainWin"];
	} else {
		foreach(ToolbarPrefs tb, lateMigrationData.toolbars["mainWin"]) {
			toolbars.insert(0, tb);
		}
	}
	foreach(ToolbarPrefs tb, toolbars) {
		PsiToolBar::structToOptions(o, tb);
	}

	// 2016-02-09 touches Psi+ users. but let it be here for awhile
	if (o->getOption("options.contactlist.use-autohide", false).toBool()) {
		o->setOption("options.contactlist.autohide-interval", 0);
		o->removeOption("options.contactlist.use-autohide");
	}
}


QString pathToProfile(const QString &name, ApplicationInfo::HomedirType type)
{
	return ApplicationInfo::profilesDir(type) + "/" + name;
}

QString pathToProfileConfig(const QString &name)
{
	return pathToProfile(name, ApplicationInfo::ConfigLocation) + "/config.xml";
}

QStringList getProfilesList()
{
	QStringList list;

	QDir d(ApplicationInfo::profilesDir(ApplicationInfo::ConfigLocation));
	if(!d.exists())
		return list;

	QStringList entries = d.entryList();
	for(QStringList::Iterator it = entries.begin(); it != entries.end(); ++it) {
		if(*it == "." || *it == "..")
			continue;
		QFileInfo info(d, *it);
		if(!info.isDir())
			continue;

		list.append(*it);
	}

	list.sort();

	return list;
}

bool profileExists(const QString &_name)
{
	QString name = _name.toLower();

	QStringList list = getProfilesList();
	for(QStringList::ConstIterator it = list.begin(); it != list.end(); ++it) {
		if((*it).toLower() == name)
			return true;
	}
	return false;
}

bool profileNew(const QString &name)
{
	if(name.isEmpty())
		return false;

	// verify the string is sane
	for(int n = 0; n < (int)name.length(); ++n) {
		if(!name.at(n).isLetterOrNumber())
			return false;
	}

	// make it
	QDir configProfilesDir(ApplicationInfo::profilesDir(ApplicationInfo::ConfigLocation));
	if(!configProfilesDir.exists())
		return false;
	QDir configCurrentProfileDir(configProfilesDir.path() + "/" + name);
	if(!configCurrentProfileDir.exists()) {
		if (!configProfilesDir.mkdir(name))
		return false;
	}

	QDir dataProfilesDir(ApplicationInfo::profilesDir(ApplicationInfo::DataLocation));
	if(!dataProfilesDir.exists())
		return false;
	QDir dataCurrentProfileDir(dataProfilesDir.path() + "/" + name);
	if(!dataCurrentProfileDir.exists()) {
		if (!dataProfilesDir.mkdir(name))
		return false;
	}
	dataCurrentProfileDir.mkdir("history");

	QDir cacheProfilesDir(ApplicationInfo::profilesDir(ApplicationInfo::CacheLocation));
	if(!cacheProfilesDir.exists())
		return false;
	QDir cacheCurrentProfileDir(cacheProfilesDir.path() + "/" + name);
	if(!cacheCurrentProfileDir.exists()) {
		if (!cacheProfilesDir.mkdir(name))
		return false;
	}
	cacheCurrentProfileDir.mkdir("vcard");

	return true;
}

bool profileRename(const QString &oldname, const QString &name)
{
	// verify the string is sane
	for(int n = 0; n < (int)name.length(); ++n) {
		if(!name.at(n).isLetterOrNumber())
			return false;
	}

	// locate the folders
	QStringList paths;
	paths << ApplicationInfo::profilesDir(ApplicationInfo::ConfigLocation);
	if(!paths.contains(ApplicationInfo::profilesDir(ApplicationInfo::DataLocation))) {
		paths << ApplicationInfo::profilesDir(ApplicationInfo::DataLocation);
	}
	if(!paths.contains(ApplicationInfo::profilesDir(ApplicationInfo::CacheLocation))) {
		paths << ApplicationInfo::profilesDir(ApplicationInfo::CacheLocation);
	}


	// First we need to check configDir for existing
	QDir configDir(paths[0]);
	if(!configDir.exists())
		return false;

	// and if all ok we may rename it.
	foreach(QString path, paths) {
		QDir d(path);
		if(!d.exists() || !d.exists(oldname))
			continue;

		if(!d.rename(oldname, name))
			return false;
	}
	return true;
}

static bool folderRemove(const QDir &_d)
{
	QDir d = _d;

	QStringList entries = d.entryList();
	for(QStringList::Iterator it = entries.begin(); it != entries.end(); ++it) {
		if(*it == "." || *it == "..")
			continue;
		QFileInfo info(d, *it);
		if(info.isDir()) {
			if(!folderRemove(QDir(info.filePath())))
				return false;
		}
		else {
			//printf("deleting [%s]\n", info.filePath().latin1());
			d.remove(info.fileName());
		}
	}
	QString name = d.dirName();
	if(!d.cdUp())
		return false;
	//printf("removing folder [%s]\n", d.filePath(name).latin1());
	d.rmdir(name);

	return true;
}

bool profileDelete(const QStringList &paths)
{
	bool ret = true;
	foreach(QString path, paths) {
		QDir d(path);
		if(!d.exists())
			continue;

		ret = folderRemove(QDir(path));
		if(!ret) {
			break;
		}
	}
	return ret;
}

QString activeProfile;
