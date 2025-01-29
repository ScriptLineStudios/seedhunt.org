from flask import Flask, render_template, session, request, flash, redirect, url_for
import random
import json
import pybiomes
from werkzeug.urls import iri_to_uri

from pymongo import MongoClient

import queue
import threading
import datetime

from seed_to_image import get_seed_data

WORK_SIZE = 1_000_000_000

seed_queue = queue.Queue()

class Database:
    def __init__(self):
        self.client = MongoClient("mongodb+srv://scriptlinestudios:T7U8tadqrsbhGSOf@seedhuntorg.sl2kn.mongodb.net/?retryWrites=true&w=majority&appName=seedhuntorg")

        self.user_db = self.client["Users"]
        self.work_db = self.client["Work"]

        self.account_col = self.user_db["Accounts"]
        self.device_col = self.user_db["Devices"]

        self.active_pool_col = self.work_db["ActivePool"]
        self.incomplete_pool_col = self.work_db["IncompletePool"] 
        self.work_state_col = self.work_db["WorkState"] 
        self.valid_seeds_col = self.work_db["Seeds"] 
        self.seeds_per_second_buckets_col = self.work_db["SeedsPerSecond"] 
        # print(list(self.seeds_per_second_buckets_col.find())[-1])
        # exit()
        #create a start bucket

    def add_seed(self, seed, finder):
        # image = get_image_for_seed(seed)
        seed_data = get_seed_data(seed)
        structure_counts = seed_data.get("structure_counts")
        biomes = seed_data.get("biomes")

        self.valid_seeds_col.insert_one({
            "_id": seed,
            "image": seed_data.get("image"),
            "biomes": biomes,
            "structure_counts": [(list(structure_counts)[i], list(structure_counts.values())[i]) for i in range(len(structure_counts))],   
            "structure_positions": seed_data.get("structure_positions"),
            "finder": finder,
            "finder_username": self.get_user(finder).get("username")
        })

    def find_seeds_that_match_criteria(self, criteria):
        # {'village': ['100', '400', '100']}
        seeds = list(self.valid_seeds_col.find_one({}))

        # for seed in seeds:
            # for structure in criteria.keys():
                # if len(seed.get("structure_positions").get(structure)) < 
                



    def add_sps_to_latest_bucket(self, sps, device_id):
        # grab the latest bucket
        bucket = list(self.seeds_per_second_buckets_col.find())[-1]
        bucket_creation_time = bucket["_id"]
        now = datetime.datetime.utcnow()
        print(now)
        print(bucket_creation_time)
        if (now - bucket_creation_time).seconds > 60: # more than a minute has passed
            print("CREATING A NEW BUCKET!")
            # create a new bucket
            self.seeds_per_second_buckets_col.insert_one({
                "_id": now,
                "sps": [] 
            })
            bucket = {
                "_id": now,
                "sps": [] 
            }
            # print(bucket, bucket_creation_time)
            bucket_creation_time = now
            assert(bucket["_id"] == bucket_creation_time)
            self.device_col.update_many( # allow contributions from all devices 
                {},
                {"$set": {
                    "has_contributed_to_bucket": False,
                }}
            )

        device = self.get_device(device_id)
        if device.get("has_contributed_to_bucket"):
            return

        device["has_contributed_to_bucket"] = True
        bucket["sps"].append(sps)

        self.update_device(device)
        self.seeds_per_second_buckets_col.update_one({"_id": bucket_creation_time}, {"$set": bucket})

    def create_new_user(self, email, username, password):
        user = self.account_col.find_one({"_id": email})
        if not user:
            self.account_col.insert_one({
                "_id": email,
                "username": username,
                "password": password,
                "active_devices": 0,
                "devices": [],
                "hash": hash(email + username + password) # used for generating device ids
            })  

    def get_user_if_valid_credentials(self, email, password):
        user = self.account_col.find_one({"_id": email})
        if user:
            user_password = user.get("password")
            if password == user_password:
                return user

    def update_user(self, user):
        self.account_col.update_one({"_id": user["_id"]}, {"$set": user})

    def create_new_device(self, device_id, email): 
        print(f"EMAIL = {email}")
        device = self.device_col.find_one({"_id": device_id})
        
        if device:
            return False

        self.device_col.insert_one({
            "_id": device_id,
            "owner": email, 
            "seeds_per_second": 0,
            "currently_working": False,
            "work_id": -1, 
            "has_contributed_to_bucket": False,
        })
        
        return True

    def get_device(self, device_id):
        return self.device_col.find_one({"_id": device_id})

    def get_user(self, email):
        return self.account_col.find_one({"_id": email})

    def update_device(self, device):
        self.device_col.update_one({"_id": device["_id"]}, {"$set": device})

    def generate_new_work(self):
        # only generate more work when all the work in the incomplete pool has been completed.
        if self.incomplete_pool_col.estimated_document_count() <= 5:
            work_state = self.work_state_col.find_one({"_id": "state"})
            latest_seed = work_state.get("latest_generated_seed")
            for wid, start_seed in enumerate(range(latest_seed, latest_seed + WORK_SIZE * 15, WORK_SIZE)):
                self.incomplete_pool_col.insert_one({
                    "_id": start_seed // WORK_SIZE,
                    "start_seed": str(start_seed),
                    "end_seed": str(start_seed + WORK_SIZE),
                })            
            work_state["latest_generated_seed"] = latest_seed + WORK_SIZE * 15
            self.work_state_col.update_one({"_id": "state"}, {"$set": work_state})

    def set_work_as_complete(self, work):
        self.active_pool_col.delete_one({"_id": work["_id"]})

    def set_work_as_active(self, work):
        self.incomplete_pool_col.delete_one({"_id": work["_id"]})
        self.active_pool_col.insert_one(work)

    def set_work_as_inactive(self, work):
        self.active_pool_col.delete_one({"_id": work["_id"]})
        self.incomplete_pool_col.insert_one(work)

    def get_work(self, wid):
        return self.active_pool_col.find_one({"_id": wid})

    def remove_device(self, device_id):
        self.device_col.delete_one({"_id": device_id})

app = Flask(__name__)
app.secret_key = "sekret"

db = Database()

# db.client.drop_database("Users")
# db.client.drop_database("Work")

if not db.work_state_col.find_one({"_id": "state"}):
    db.work_state_col.insert_one({
        "_id": "state",
        "latest_complete_seed": 0,
        "latest_generated_seed": 0,
        "complete_work": [],
    })

db.create_new_user("scriptlinestudios@protonmail.com", "ScriptLine", "k>^,WHR2N9*H6")
db.generate_new_work()

db.seeds_per_second_buckets_col.insert_one({
    "_id": datetime.datetime.utcnow(),
    "sps": [] 
})

def handle_incoming_seeds():
    while True:
        seed_data = seed_queue.get()
        seed = seed_data.get("seed")
        finder = seed_data.get("finder")
        db.add_seed(seed, finder)

thread = threading.Thread(target=handle_incoming_seeds)
thread.start()

@app.route("/leaderboard")
def leaderboard():
    devices = list(db.device_col.find({}))
    owners = {}
    for device in devices:
        owner = db.get_user(device.get("owner")).get("username")
        if not owners.get(owner):
            owners[owner] = 0
        owners[owner] += device.get("seeds_per_second")

    return render_template("leaderboard.html", entries=sorted(owners.items(), key = lambda x: x[1], reverse=True))

@app.route("/")
def home():
    seeds = list(db.valid_seeds_col.find({}))
    return render_template("index.html", seeds=seeds)

@app.route("/stats")
def stats():
    buckets = list(db.seeds_per_second_buckets_col.find())[-30:]
    sps = []
    x_values = []
    for bucket in buckets:
        sps.append(sum(bucket["sps"]))
        x_values.append(str(bucket["_id"].ctime()))
    print(x_values)
    # x_values = [i for i in range(len(sps))]
    y_values = sps
    return render_template("stats.html", x_values=x_values, y_values=y_values)

@app.route("/account")
def account():
    print(session)
    if session.get("email"):
        account_devices = db.device_col.find({ "owner": session["email"]})
        devices = []
        for device in list(account_devices):
            devices.append(device)
        print(devices)
        return render_template("account_signed_in.html", user=session.get("email"), devices=devices)

    return render_template("account_signed_out.html", user=session.get("email"))

@app.route("/signup")
def signup():
    return render_template("signup.html")
    
@app.route("/signin")
def signin():
    return render_template("signin.html")

@app.route("/search")
def search():
    return render_template("search.html")

@app.route("/lookup")
def lookup():
    raw_url = iri_to_uri(request.url)
    pieces = raw_url.split("&")
    criterias = [pieces[i:i+2] for i in range(1, len(pieces), 3)]

    criteria_dict = {}

    for criteria in criterias:
        structure = criteria[0].split("=")[1]
        distance = criteria[1].split("=")[1]

        if not criteria_dict.get(structure):
            criteria_dict[structure] = []

        criteria_dict[structure].append(distance)

    print(db.find_seeds_that_match_criteria(criteria_dict))
    return ""

@app.route("/seeds/<seed>")
def seeds(seed):
    seed_data = db.valid_seeds_col.find_one({"_id": int(seed)})
    return render_template("seeds.html", seed=seed_data)

@app.route("/signin_account", methods=["POST"])
def signin_account():
    email = request.form.get("email")
    password = request.form.get("password")
    
    user = db.get_user_if_valid_credentials(email, password)
    if not user:
        flash("Account with that password doesn't exist!")
        return redirect(url_for("signin"))

    session["email"] = email

    return redirect(url_for("home"))

@app.route("/signup_account", methods=["POST"])
def signup_account():
    username = request.form.get("display_name")
    email = request.form.get("email")
    password = request.form.get("password")
    password_repeat = request.form.get("password_confirm")

    if password != password_repeat:
        flash("Passwords don't match!")
        return redirect(url_for("signup"))

    db.create_new_user(email, username, password)
    session["email"] = email # this will soon change :)

    return redirect(url_for("home"))

@app.route("/device_sign_out", methods=["POST"])
def device_signout():  
    content = json.loads(str(request.stream.read().decode()))
    device_id = int(content.get("device_id"))

    # email = temp_database["devices"][device_id]["owner"]
    # temp_database["users"].get(email)["active_devices"] -= 1

    device = db.get_device(device_id)
    user = db.get_user(device.get("owner"))
    user["active_devices"] -= 1
    try:
        user["devices"].remove(device_id)
    except ValueError:
        pass
    db.update_user(user)

    if device.get("currently_working"):
        work_assignment = db.get_work(device.get("work_id"))
        db.set_work_as_inactive(work_assignment)
        device["currently_working"] = False
        db.update_device(device)

    db.remove_device(device_id)

    # # if a device that signs out has work, move it back into incomplete
    # if temp_database["devices"][device_id]["has_work"]:
    #     wid = temp_database["devices"][device_id]["work_id"]
    #     for work_block in temp_database["work"]["active_pool"]:
    #         if work_block["id"] == wid:
    #             temp_database["work"]["active_pool"].remove(work_block)
    #             temp_database["work"]["incomplete_blocks"].append(work_block)

    # del temp_database["devices"][device_id]

    return {}, 200

@app.route("/device_submit_work", methods=["POST"])
def device_submit_work():
    content = json.loads(str(request.stream.read().decode()))
    seeds = content.get("results")
    device_id = content.get("device_id")

    device = db.get_device(device_id)
    if device.get("currently_working"):
        work_assignment = db.get_work(device.get("work_id"))
        db.set_work_as_complete(work_assignment)
        device["currently_working"] = False
        db.update_device(device)

    for seed in seeds:
        seed_queue.put({
            "seed": seed,
            "finder": device.get("owner"), 
        })

    return {}, 200

@app.route("/device_get_work", methods=["POST"])
def device_get_work():
    content = json.loads(str(request.stream.read().decode()))
    device_id = int(content.get("device_id"))
    print(device_id)

    if db.get_device(device_id).get("currently_working"):
        return db.active_pool_col.find_one({ "_id": db.get_device(device_id).get("work_id") }), 200
        # return {
        #     "message": "[SERVER]: Device already has work!" 
        # }, 401

    db.generate_new_work() # ensure there is work for clients to do...

    work_assignment = db.incomplete_pool_col.find_one()
    db.set_work_as_active(work_assignment)

    device = db.get_device(device_id)
    device["currently_working"] = True
    device["work_id"] = work_assignment.get("_id")
    db.update_device(device)
    
    return work_assignment, 200

@app.route("/device_report_sps", methods=["POST"])
def device_report_sps():
    content = json.loads(str(request.stream.read().decode()))
    device_id = content.get("device_id")
    sps = content.get("sps")

    print(f"sps for {device_id} = {sps}")

    device = db.get_device(device_id)
    device["seeds_per_second"] = sps
    db.update_device(device)

    db.add_sps_to_latest_bucket(sps, device_id)

    return {}, 200

@app.route("/device_signin", methods=["POST"])
def device_signin():  
    content = json.loads(str(request.stream.read().decode()))
    email = content.get("email")
    password = content.get("password")
    client_device_id = content.get("client_device_id")

    user = db.get_user_if_valid_credentials(email, password)
    if not user:
        return {
            "message": f"[SERVER]: Account with these details not found!",
            "device_id": -1,
        }, 401

    # register a new device
    seed = user["hash"] + client_device_id
    random.seed(seed)
    device_id = random.randrange(0, 1<<16)
    print(f"DEVICE ID = {device_id}")

    # if the device we are trying to create already exists, it means the user didn't sign out correctly and they can have this device reassigned to them
    if db.create_new_device(device_id, email):
        user["active_devices"] += 1
        user["devices"].append(device_id)
        db.update_user(user)        

    return {
        "message": f"[SERVER]: Welcome {user.get('username')}!",
        "device_id": device_id
    }

if __name__ == "__main__":
    app.run(debug=True)

